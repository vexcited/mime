/*
 * the compiler from dxbc to spir-v.
 *
 * registers are private vec4 variables of floats, other types reached by
 * bitcasts, as dxbc's registers have no type. inputs and outputs are
 * private arrays the entry point fills from the interface before the
 * shader's body runs and copies to the interface after it, so system
 * values, packing and relative indexing stay simple. the body is a
 * function of its own, ret returning from it; subroutines are functions.
 *
 * resources are reached through mime's heap: the stage's table gives the
 * heap index of each slot, and variables of every image type the shader
 * needs alias the heap's arrays.
 */
#include "internal.h"
#include "spvbuild.h"

#include "GLSL.std.450.h"

typedef enum Kind { K_FLOAT, K_UINT, K_INT, K_KINDS } Kind;

#define MAX_REGS 64
#define MAX_TEMPS 4096
#define MAX_LABELS 256

typedef struct Resource {
    int declared;
    uint32_t dim;
    Kind kind;
    uint32_t stride;
    int raw;
    int structured;
    int counter;
    int atomics;
} Resource;

typedef struct Flow {
    int kind;
    uint32_t merge;
    uint32_t cont;
    uint32_t header;
    uint32_t else_label;
    /* switches: the label of each case in order, and where the next case is in the list. */
    uint32_t *labels;
    uint32_t *values;
    uint32_t count;
    uint32_t next;
    uint32_t default_label;
    uint32_t open_label;
} Flow;

enum { FLOW_IF, FLOW_LOOP, FLOW_SWITCH };

typedef struct InputDecl {
    int declared;
    /* the components read from the interface, and those the system value fills. */
    uint8_t mask;
    uint8_t sv_mask;
    uint32_t sv;
    uint32_t interpolation;
    uint32_t var;
} InputDecl;

typedef struct OutputDecl {
    int declared;
    uint8_t mask;
    uint32_t sv;
    uint32_t var;
    Kind kind;
} OutputDecl;

typedef struct Compiler {
    SpvBuilder b;
    const MimeAllocator *alloc;
    DxbcShaderInfo *info;
    const DxbcCompileOptions *options;
    DxbcProgram program;
    uint32_t binding;
    const uint32_t *code;
    uint32_t code_count;
    const char *error;

    /* types. */
    uint32_t t_void, t_bool, t_f32, t_u32, t_i32;
    uint32_t t_vec[K_KINDS][5];
    uint32_t t_bvec[5];

    /* registers. */
    uint32_t temps[MAX_TEMPS];
    uint32_t temp_count;
    uint32_t indexable[256];
    uint32_t indexable_size[256];
    uint32_t inputs, outputs;
    uint32_t input_count, output_count;
    InputDecl input_decl[MAX_REGS];
    OutputDecl output_decl[MAX_REGS];
    uint32_t depth_output, coverage_output;
    uint32_t icb;
    uint32_t icb_size;
    uint32_t tgsm[256];
    uint32_t tgsm_size[256];
    uint32_t tgsm_stride[256];

    /* the interface the entry point lists. */
    uint32_t interface[256];
    uint32_t interface_count;

    /* the heap and the table. */
    uint32_t table;
    uint32_t cb_heap;
    uint32_t sampler_heap;
    struct {
        uint32_t image_type;
        uint32_t var;
    } image_vars[64];
    uint32_t image_var_count;
    Resource srv[128];
    Resource uav[64];
    uint32_t sampler_mode[16];

    /* built-in variables, made when first used. */
    uint32_t builtins[64];

    /* functions. */
    uint32_t body;
    /* subroutines: their label numbers, where their code starts, their functions. */
    uint32_t label_number[MAX_LABELS];
    uint32_t label_at[MAX_LABELS];
    uint32_t label_fn[MAX_LABELS];
    uint32_t label_count;

    Flow flow[64];
    uint32_t flow_depth;

    uint32_t global_flags;
    uint32_t thread_group[3];
} Compiler;

static int fail(Compiler *c, const char *error)
{
    if (!c->error)
        c->error = error;
    return -1;
}

/* ------------------------------------------------------------------ types and helpers */

static uint32_t scalar_type(Compiler *c, Kind k) { return k == K_FLOAT ? c->t_f32 : k == K_UINT ? c->t_u32 : c->t_i32; }

static uint32_t vec_type(Compiler *c, Kind k, uint32_t n) { return c->t_vec[k][n]; }

static uint32_t uconst(Compiler *c, uint32_t v) { return spv_const(&c->b, c->t_u32, v); }

static uint32_t iconst(Compiler *c, int32_t v) { return spv_const(&c->b, c->t_i32, (uint32_t)v); }

static uint32_t fconst(Compiler *c, float v)
{
    uint32_t bits;
    __builtin_memcpy(&bits, &v, 4);
    return spv_const(&c->b, c->t_f32, bits);
}

/* a vector of the kind with each component the same constant bits. */
static uint32_t splat_const(Compiler *c, Kind k, uint32_t n, uint32_t bits)
{
    uint32_t s = spv_const(&c->b, scalar_type(c, k), bits);
    if (n == 1)
        return s;
    uint32_t parts[4] = {s, s, s, s};
    return spv_const_composite(&c->b, vec_type(c, k, n), parts, n);
}

static uint32_t splat(Compiler *c, Kind k, uint32_t n, uint32_t scalar)
{
    if (n == 1)
        return scalar;
    uint32_t parts[4] = {scalar, scalar, scalar, scalar};
    return spv_op(&c->b, SpvOpCompositeConstruct, vec_type(c, k, n), parts, n);
}

static uint32_t bitcast(Compiler *c, uint32_t value, Kind from, Kind to, uint32_t n)
{
    if (from == to)
        return value;
    return spv_op1(&c->b, SpvOpBitcast, vec_type(c, to, n), value);
}

static uint32_t extract(Compiler *c, uint32_t type, uint32_t value, uint32_t index)
{
    return spv_op2(&c->b, SpvOpCompositeExtract, type, value, index);
}

static uint32_t popcount(uint32_t v) { return (uint32_t)__builtin_popcount(v); }

static void add_interface(Compiler *c, uint32_t var)
{
    if (c->interface_count < 256)
        c->interface[c->interface_count++] = var;
}

static uint32_t global(Compiler *c, uint32_t storage, uint32_t type, uint32_t initializer)
{
    uint32_t var = spv_variable(&c->b, spv_type_pointer(&c->b, storage, type), storage, initializer);
    add_interface(c, var);
    return var;
}

static void decorate1(Compiler *c, uint32_t id, uint32_t decoration, uint32_t value)
{
    spv_decorate(&c->b, id, decoration, &value, 1);
}

/* the boolean of a d3d condition: a register component compared with zero. */
static uint32_t nonzero(Compiler *c, uint32_t value, uint32_t n)
{
    return spv_op2(&c->b, SpvOpINotEqual, c->t_bvec[n], value, splat_const(c, K_UINT, n, 0));
}

static uint32_t bool_to_mask(Compiler *c, uint32_t value, uint32_t n)
{
    return spv_op3(&c->b, SpvOpSelect, vec_type(c, K_UINT, n), value, splat_const(c, K_UINT, n, ~0u),
                   splat_const(c, K_UINT, n, 0));
}

/* ------------------------------------------------------------------ the table and the heap */

/* the table entry, a heap index. */
static uint32_t table_entry(Compiler *c, uint32_t entry)
{
    uint32_t indices[3] = {uconst(c, 0), uconst(c, entry / 4), uconst(c, entry % 4)};
    uint32_t ptr = spv_access(&c->b, spv_type_pointer(&c->b, SpvStorageClassUniform, c->t_u32), c->table, indices, 3);
    return spv_load(&c->b, c->t_u32, ptr);
}

/* the heap's array of an image type: a variable aliasing the binding of its descriptor kind. */
static uint32_t image_heap(Compiler *c, uint32_t image_type, uint32_t binding)
{
    for (uint32_t i = 0; i < c->image_var_count; i++)
        if (c->image_vars[i].image_type == image_type)
            return c->image_vars[i].var;
    uint32_t array = spv_type_runtime_array(&c->b, image_type);
    uint32_t var = global(c, SpvStorageClassUniformConstant, array, 0);
    decorate1(c, var, SpvDecorationDescriptorSet, DXBC_SET_HEAP);
    decorate1(c, var, SpvDecorationBinding, binding);
    if (c->image_var_count < 64) {
        c->image_vars[c->image_var_count].image_type = image_type;
        c->image_vars[c->image_var_count].var = var;
        c->image_var_count++;
    }
    return var;
}

static uint32_t sampled_kind_type(Compiler *c, Kind k) { return scalar_type(c, k); }

/* the spir-v dimension, arrayness and multisampling of a resource dimension. */
static void image_shape(uint32_t dim, uint32_t *spv_dim, uint32_t *arrayed, uint32_t *ms)
{
    *arrayed = *ms = 0;
    switch (dim) {
    case RES_BUFFER:
    case RES_RAW_BUFFER:
    case RES_STRUCTURED_BUFFER:
        *spv_dim = SpvDimBuffer;
        break;
    case RES_TEXTURE1D:
        *spv_dim = SpvDim1D;
        break;
    case RES_TEXTURE1DARRAY:
        *spv_dim = SpvDim1D, *arrayed = 1;
        break;
    case RES_TEXTURE2DMS:
        *spv_dim = SpvDim2D, *ms = 1;
        break;
    case RES_TEXTURE2DMSARRAY:
        *spv_dim = SpvDim2D, *ms = 1, *arrayed = 1;
        break;
    case RES_TEXTURE2DARRAY:
        *spv_dim = SpvDim2D, *arrayed = 1;
        break;
    case RES_TEXTURE3D:
        *spv_dim = SpvDim3D;
        break;
    case RES_TEXTURECUBE:
        *spv_dim = SpvDimCube;
        break;
    case RES_TEXTURECUBEARRAY:
        *spv_dim = SpvDimCube, *arrayed = 1;
        break;
    default:
        *spv_dim = SpvDim2D;
        break;
    }
}

static void image_capabilities(Compiler *c, uint32_t spv_dim, uint32_t arrayed, uint32_t ms, int storage)
{
    if (spv_dim == SpvDim1D)
        spv_capability(&c->b, storage ? SpvCapabilityImage1D : SpvCapabilitySampled1D);
    if (spv_dim == SpvDimBuffer)
        spv_capability(&c->b, storage ? SpvCapabilityImageBuffer : SpvCapabilitySampledBuffer);
    if (spv_dim == SpvDimCube && arrayed)
        spv_capability(&c->b, storage ? SpvCapabilityImageCubeArray : SpvCapabilitySampledCubeArray);
    if (ms && arrayed && storage)
        spv_capability(&c->b, SpvCapabilityImageMSArray);
}

/* the image type of a shader resource view slot, and the heap binding of its descriptors. */
static uint32_t srv_image_type(Compiler *c, uint32_t slot, int depth, uint32_t *binding)
{
    Resource *r = &c->srv[slot];
    uint32_t spv_dim, arrayed, ms;
    image_shape(r->dim, &spv_dim, &arrayed, &ms);
    image_capabilities(c, spv_dim, arrayed, ms, 0);
    Kind k = r->raw || r->structured ? K_UINT : r->kind;
    *binding = spv_dim == SpvDimBuffer ? DXBC_HEAP_UNIFORM_TEXEL : DXBC_HEAP_SAMPLED_IMAGE;
    return spv_type_image(&c->b, sampled_kind_type(c, k), spv_dim, depth ? 1 : 0, arrayed, ms, 1,
                          SpvImageFormatUnknown);
}

static uint32_t uav_image_type(Compiler *c, uint32_t slot, uint32_t *binding)
{
    Resource *r = &c->uav[slot];
    uint32_t spv_dim, arrayed, ms;
    image_shape(r->dim, &spv_dim, &arrayed, &ms);
    image_capabilities(c, spv_dim, arrayed, ms, 1);
    Kind k = r->raw || r->structured ? K_UINT : r->kind;
    *binding = spv_dim == SpvDimBuffer ? DXBC_HEAP_STORAGE_TEXEL : DXBC_HEAP_STORAGE_IMAGE;
    /* atomics need the format spelled out; plain loads and stores work without it. */
    uint32_t format = SpvImageFormatUnknown;
    if (r->raw || r->structured || r->atomics)
        format = k == K_INT ? SpvImageFormatR32i : SpvImageFormatR32ui;
    else {
        spv_capability(&c->b, SpvCapabilityStorageImageReadWithoutFormat);
        spv_capability(&c->b, SpvCapabilityStorageImageWriteWithoutFormat);
    }
    return spv_type_image(&c->b, sampled_kind_type(c, k), spv_dim, 0, arrayed, ms, 2, format);
}

/* a pointer to the heap's descriptor of a slot, through the table. */
static uint32_t descriptor_pointer(Compiler *c, uint32_t image_type, uint32_t binding, uint32_t entry)
{
    uint32_t heap = image_heap(c, image_type, binding);
    uint32_t index = table_entry(c, entry);
    uint32_t ptr_type = spv_type_pointer(&c->b, SpvStorageClassUniformConstant, image_type);
    return spv_access(&c->b, ptr_type, heap, &index, 1);
}

static uint32_t load_srv(Compiler *c, uint32_t slot, int depth, uint32_t *type)
{
    uint32_t binding;
    *type = srv_image_type(c, slot, depth, &binding);
    return spv_load(&c->b, *type, descriptor_pointer(c, *type, binding, DXBC_TABLE_SRV + slot));
}

static uint32_t uav_pointer(Compiler *c, uint32_t slot, uint32_t *type)
{
    uint32_t binding;
    *type = uav_image_type(c, slot, &binding);
    return descriptor_pointer(c, *type, binding, DXBC_TABLE_UAV + slot);
}

static uint32_t load_sampler(Compiler *c, uint32_t slot)
{
    uint32_t type = spv_type_sampler(&c->b);
    if (!c->sampler_heap) {
        c->sampler_heap = global(c, SpvStorageClassUniformConstant, spv_type_runtime_array(&c->b, type), 0);
        decorate1(c, c->sampler_heap, SpvDecorationDescriptorSet, DXBC_SET_HEAP);
        decorate1(c, c->sampler_heap, SpvDecorationBinding, DXBC_HEAP_SAMPLER);
    }
    uint32_t index = table_entry(c, DXBC_TABLE_SAMPLER + slot);
    uint32_t ptr = spv_access(&c->b, spv_type_pointer(&c->b, SpvStorageClassUniformConstant, type), c->sampler_heap,
                              &index, 1);
    return spv_load(&c->b, type, ptr);
}

/* a pointer to vec4 i of constant buffer slot, i a uint value. */
static uint32_t cb_pointer(Compiler *c, uint32_t slot, uint32_t index)
{
    if (!c->cb_heap) {
        uint32_t array = spv_type_array_unique(&c->b, c->t_vec[K_FLOAT][4], 4096);
        decorate1(c, array, SpvDecorationArrayStride, 16);
        uint32_t block = spv_type_struct_unique(&c->b, &array, 1);
        uint32_t offset = 0;
        spv_member_decorate(&c->b, block, 0, SpvDecorationOffset, &offset, 1);
        spv_decorate(&c->b, block, SpvDecorationBlock, NULL, 0);
        c->cb_heap = global(c, SpvStorageClassUniform, spv_type_runtime_array(&c->b, block), 0);
        decorate1(c, c->cb_heap, SpvDecorationDescriptorSet, DXBC_SET_HEAP);
        decorate1(c, c->cb_heap, SpvDecorationBinding, DXBC_HEAP_UNIFORM_BUFFER);
    }
    uint32_t heap_index = table_entry(c, DXBC_TABLE_CB + slot);
    uint32_t indices[3] = {heap_index, uconst(c, 0), index};
    return spv_access(&c->b, spv_type_pointer(&c->b, SpvStorageClassUniform, c->t_vec[K_FLOAT][4]), c->cb_heap,
                      indices, 3);
}

/* ------------------------------------------------------------------ operands */

static uint32_t load_scalar(Compiler *c, const DxbcOperand *op, uint32_t component, Kind k);

/* the value of an index: its immediate plus its register, as a uint. */
static uint32_t index_value(Compiler *c, const DxbcIndex *index)
{
    uint32_t imm = uconst(c, index->immediate);
    if (!index->relative)
        return imm;
    uint32_t rel = load_scalar(c, index->relative, index->relative->swizzle[0], K_UINT);
    return index->immediate ? spv_op2(&c->b, SpvOpIAdd, c->t_u32, rel, imm) : rel;
}

/* a pointer to the vec4 of floats an operand names, for registers kept in variables. */
static uint32_t register_pointer(Compiler *c, const DxbcOperand *op)
{
    uint32_t vec4 = c->t_vec[K_FLOAT][4];
    uint32_t ptr = spv_type_pointer(&c->b, SpvStorageClassPrivate, vec4);
    switch (op->type) {
    case OPERAND_TEMP:
        if (op->index[0].immediate >= c->temp_count) {
            fail(c, "temporary register out of range");
            return 0;
        }
        return c->temps[op->index[0].immediate];
    case OPERAND_INDEXABLE_TEMP: {
        uint32_t reg = op->index[0].immediate & 255;
        if (!c->indexable[reg]) {
            fail(c, "undeclared indexable temporary");
            return 0;
        }
        uint32_t i = index_value(c, &op->index[1]);
        return spv_access(&c->b, ptr, c->indexable[reg], &i, 1);
    }
    case OPERAND_INPUT: {
        uint32_t i = index_value(c, &op->index[0]);
        return spv_access(&c->b, ptr, c->inputs, &i, 1);
    }
    case OPERAND_OUTPUT: {
        uint32_t i = index_value(c, &op->index[0]);
        return spv_access(&c->b, ptr, c->outputs, &i, 1);
    }
    default:
        fail(c, "operand type not held in a register");
        return 0;
    }
}

static uint32_t builtin(Compiler *c, uint32_t which, uint32_t storage, uint32_t type);

/* the system value operands of compute and pixel shaders, as a uvec4 or float vec4. */
static uint32_t system_operand(Compiler *c, const DxbcOperand *op, Kind *kind)
{
    uint32_t uvec3 = c->t_vec[K_UINT][3], u4 = c->t_vec[K_UINT][4];
    uint32_t v = 0;
    *kind = K_UINT;
    switch (op->type) {
    case OPERAND_INPUT_THREAD_ID:
        v = spv_load(&c->b, uvec3, builtin(c, SpvBuiltInGlobalInvocationId, SpvStorageClassInput, uvec3));
        break;
    case OPERAND_INPUT_THREAD_GROUP_ID:
        v = spv_load(&c->b, uvec3, builtin(c, SpvBuiltInWorkgroupId, SpvStorageClassInput, uvec3));
        break;
    case OPERAND_INPUT_THREAD_ID_IN_GROUP:
        v = spv_load(&c->b, uvec3, builtin(c, SpvBuiltInLocalInvocationId, SpvStorageClassInput, uvec3));
        break;
    case OPERAND_INPUT_THREAD_ID_IN_GROUP_FLATTENED: {
        uint32_t s = spv_load(&c->b, c->t_u32,
                              builtin(c, SpvBuiltInLocalInvocationIndex, SpvStorageClassInput, c->t_u32));
        return splat(c, K_UINT, 4, s);
    }
    case OPERAND_INPUT_PRIMITIVEID: {
        uint32_t s = spv_load(&c->b, c->t_i32, builtin(c, SpvBuiltInPrimitiveId, SpvStorageClassInput, c->t_i32));
        return splat(c, K_UINT, 4, spv_op1(&c->b, SpvOpBitcast, c->t_u32, s));
    }
    case OPERAND_INPUT_COVERAGE_MASK: {
        uint32_t arr = spv_type_array(&c->b, c->t_i32, 1);
        uint32_t var = builtin(c, SpvBuiltInSampleMask, SpvStorageClassInput, arr);
        uint32_t zero = iconst(c, 0);
        uint32_t ptr = spv_access(&c->b, spv_type_pointer(&c->b, SpvStorageClassInput, c->t_i32), var, &zero, 1);
        uint32_t s = spv_load(&c->b, c->t_i32, ptr);
        return splat(c, K_UINT, 4, spv_op1(&c->b, SpvOpBitcast, c->t_u32, s));
    }
    default:
        fail(c, "system value operand not implemented");
        return splat_const(c, K_UINT, 4, 0);
    }
    uint32_t zero = uconst(c, 0);
    uint32_t parts[4] = {extract(c, c->t_u32, v, 0), extract(c, c->t_u32, v, 1), extract(c, c->t_u32, v, 2), zero};
    return spv_op(&c->b, SpvOpCompositeConstruct, u4, parts, 4);
}

/* the whole vec4 an operand reads, before its swizzle, as the kind it is stored in. */
static uint32_t load_vec4(Compiler *c, const DxbcOperand *op, Kind *stored)
{
    uint32_t vec4 = c->t_vec[K_FLOAT][4];
    *stored = K_FLOAT;
    switch (op->type) {
    case OPERAND_TEMP:
    case OPERAND_INDEXABLE_TEMP:
    case OPERAND_INPUT:
    case OPERAND_OUTPUT: {
        uint32_t ptr = register_pointer(c, op);
        return ptr ? spv_load(&c->b, vec4, ptr) : splat_const(c, K_FLOAT, 4, 0);
    }
    case OPERAND_IMMEDIATE32:
    case OPERAND_IMMEDIATE64: {
        *stored = K_UINT;
        uint32_t parts[4];
        for (int i = 0; i < 4; i++)
            parts[i] = uconst(c, op->immediate[i]);
        return spv_const_composite(&c->b, c->t_vec[K_UINT][4], parts, 4);
    }
    case OPERAND_CONSTANT_BUFFER: {
        uint32_t slot = op->index[0].immediate;
        if (slot >= 14) {
            fail(c, "constant buffer slot out of range");
            return splat_const(c, K_FLOAT, 4, 0);
        }
        return spv_load(&c->b, vec4, cb_pointer(c, slot, index_value(c, &op->index[1])));
    }
    case OPERAND_IMMEDIATE_CONSTANT_BUFFER: {
        *stored = K_UINT;
        if (!c->icb) {
            fail(c, "immediate constant buffer read without one");
            return splat_const(c, K_UINT, 4, 0);
        }
        uint32_t i = index_value(c, &op->index[0]);
        uint32_t ptr = spv_access(&c->b, spv_type_pointer(&c->b, SpvStorageClassPrivate, c->t_vec[K_UINT][4]),
                                  c->icb, &i, 1);
        return spv_load(&c->b, c->t_vec[K_UINT][4], ptr);
    }
    default: {
        Kind k;
        uint32_t v = system_operand(c, op, &k);
        *stored = k;
        return v;
    }
    }
}

/* applies a source modifier to a value of the kind. */
static uint32_t modify(Compiler *c, uint32_t value, Kind k, uint32_t n, uint32_t modifier)
{
    uint32_t type = vec_type(c, k, n);
    if (modifier & MOD_ABS) {
        uint32_t instruction = k == K_FLOAT ? GLSLstd450FAbs : GLSLstd450SAbs;
        if (k == K_UINT) {
            uint32_t signed_value = bitcast(c, value, K_UINT, K_INT, n);
            value = bitcast(c, spv_glsl(&c->b, vec_type(c, K_INT, n), GLSLstd450SAbs, &signed_value, 1), K_INT, K_UINT,
                            n);
        } else
            value = spv_glsl(&c->b, type, instruction, &value, 1);
    }
    if (modifier & MOD_NEG) {
        if (k == K_FLOAT)
            value = spv_op1(&c->b, SpvOpFNegate, type, value);
        else
            value = spv_op1(&c->b, SpvOpSNegate, type, value);
    }
    return value;
}

/*
 * the components of a source operand that the destination mask writes,
 * swizzled, as n components of the kind asked: dxbc picks source
 * component swizzle[i] for destination component i.
 */
static uint32_t load(Compiler *c, const DxbcOperand *op, uint8_t mask, Kind k)
{
    Kind stored;
    uint32_t v = load_vec4(c, op, &stored);
    uint32_t n = popcount(mask);
    if (!n)
        n = 1, mask = 1;
    v = bitcast(c, v, stored, k, 4);
    uint32_t type = vec_type(c, k, n);
    uint32_t picks[4], count = 0;
    for (uint32_t i = 0; i < 4; i++)
        if (mask & (1 << i))
            picks[count++] = op->swizzle[i];
    if (n == 1)
        v = extract(c, type, v, picks[0]);
    else {
        uint32_t w[6] = {v, v, picks[0], picks[1], picks[2], picks[3]};
        v = spv_op(&c->b, SpvOpVectorShuffle, type, w, 2 + n);
    }
    return op->modifier ? modify(c, v, k, n, op->modifier) : v;
}

/* one component of an operand, that of swizzle position component. */
static uint32_t load_scalar(Compiler *c, const DxbcOperand *op, uint32_t component, Kind k)
{
    DxbcOperand copy = *op;
    copy.swizzle[0] = (uint8_t)component;
    return load(c, &copy, 1, k);
}

/* the first n components of an operand, in swizzle order, as for addresses and coordinates. */
static uint32_t load_first(Compiler *c, const DxbcOperand *op, uint32_t n, Kind k)
{
    return load(c, op, (uint8_t)((1 << n) - 1), k);
}

static void store_output_special(Compiler *c, const DxbcOperand *op, uint32_t value, Kind k);

/*
 * writes n components of the kind to the destination's mask, saturated if
 * the instruction says so.
 */
static void store(Compiler *c, const DxbcInstruction *ins, const DxbcOperand *op, uint32_t value, Kind k)
{
    if (op->type == OPERAND_NULL)
        return;
    uint8_t mask = op->mask ? op->mask : 1;
    uint32_t n = popcount(mask);
    if (ins && ins->saturate && k == K_FLOAT) {
        uint32_t args[3] = {value, splat_const(c, K_FLOAT, n, 0), splat_const(c, K_FLOAT, n, 0x3F800000)};
        value = spv_glsl(&c->b, vec_type(c, K_FLOAT, n), GLSLstd450NClamp, args, 3);
    }
    if (op->type == OPERAND_OUTPUT_DEPTH || op->type == OPERAND_OUTPUT_DEPTH_GREATER_EQUAL ||
        op->type == OPERAND_OUTPUT_DEPTH_LESS_EQUAL || op->type == OPERAND_OUTPUT_COVERAGE_MASK) {
        store_output_special(c, op, value, k);
        return;
    }
    uint32_t ptr = register_pointer(c, op);
    if (!ptr)
        return;
    value = bitcast(c, value, k, K_FLOAT, n);
    uint32_t fptr = spv_type_pointer(&c->b, SpvStorageClassPrivate, c->t_f32);
    uint32_t j = 0;
    for (uint32_t i = 0; i < 4; i++) {
        if (!(mask & (1 << i)))
            continue;
        uint32_t comp = n == 1 ? value : extract(c, c->t_f32, value, j);
        uint32_t index = uconst(c, i);
        spv_store(&c->b, spv_access(&c->b, fptr, ptr, &index, 1), comp);
        j++;
    }
}

/* a value of n components spread to a destination mask from a full vec4 of results. */
static uint32_t pick(Compiler *c, uint32_t vec4, Kind k, uint8_t mask, const uint8_t *swizzle)
{
    uint32_t n = popcount(mask);
    uint32_t picks[4], count = 0;
    for (uint32_t i = 0; i < 4; i++)
        if (mask & (1 << i))
            picks[count++] = swizzle ? swizzle[i] : i;
    if (n == 1)
        return extract(c, scalar_type(c, k), vec4, picks[0]);
    uint32_t w[6] = {vec4, vec4, picks[0], picks[1], picks[2], picks[3]};
    return spv_op(&c->b, SpvOpVectorShuffle, vec_type(c, k, n), w, 2 + n);
}

static void decorate_precise(Compiler *c, const DxbcInstruction *ins, uint32_t id)
{
    if (ins->precise)
        spv_decorate(&c->b, id, SpvDecorationNoContraction, NULL, 0);
}

/* ------------------------------------------------------------------ built-ins and the interface */

static uint32_t builtin(Compiler *c, uint32_t which, uint32_t storage, uint32_t type)
{
    if (which < 64 && c->builtins[which])
        return c->builtins[which];
    uint32_t var = global(c, storage, type, 0);
    decorate1(c, var, SpvDecorationBuiltIn, which);
    if (storage == SpvStorageClassInput && c->program == DXBC_PIXEL && type != c->t_bool &&
        (which == SpvBuiltInPrimitiveId || which == SpvBuiltInSampleId || which == SpvBuiltInLayer ||
         which == SpvBuiltInViewportIndex))
        spv_decorate(&c->b, var, SpvDecorationFlat, NULL, 0);
    if (which < 64)
        c->builtins[which] = var;
    return var;
}

/* the system value of an input register, as uint bits or floats. */
static uint32_t system_input(Compiler *c, uint32_t sv, Kind *kind)
{
    *kind = K_UINT;
    switch (sv) {
    case SV_POSITION: {
        *kind = K_FLOAT;
        uint32_t v4 = c->t_vec[K_FLOAT][4];
        uint32_t v = spv_load(&c->b, v4, builtin(c, SpvBuiltInFragCoord, SpvStorageClassInput, v4));
        /* direct3d's w is the clip w, vulkan's its reciprocal. */
        uint32_t w = spv_op2(&c->b, SpvOpFDiv, c->t_f32, fconst(c, 1.0f), extract(c, c->t_f32, v, 3));
        return spv_op3(&c->b, SpvOpCompositeInsert, v4, w, v, 3);
    }
    case SV_VERTEX_ID: {
        spv_capability(&c->b, SpvCapabilityDrawParameters);
        uint32_t index = spv_load(&c->b, c->t_i32, builtin(c, SpvBuiltInVertexIndex, SpvStorageClassInput, c->t_i32));
        uint32_t base = spv_load(&c->b, c->t_i32, builtin(c, SpvBuiltInBaseVertex, SpvStorageClassInput, c->t_i32));
        return spv_op1(&c->b, SpvOpBitcast, c->t_u32, spv_op2(&c->b, SpvOpISub, c->t_i32, index, base));
    }
    case SV_INSTANCE_ID: {
        spv_capability(&c->b, SpvCapabilityDrawParameters);
        uint32_t index =
            spv_load(&c->b, c->t_i32, builtin(c, SpvBuiltInInstanceIndex, SpvStorageClassInput, c->t_i32));
        uint32_t base = spv_load(&c->b, c->t_i32, builtin(c, SpvBuiltInBaseInstance, SpvStorageClassInput, c->t_i32));
        return spv_op1(&c->b, SpvOpBitcast, c->t_u32, spv_op2(&c->b, SpvOpISub, c->t_i32, index, base));
    }
    case SV_IS_FRONT_FACE: {
        uint32_t v = spv_load(&c->b, c->t_bool, builtin(c, SpvBuiltInFrontFacing, SpvStorageClassInput, c->t_bool));
        return spv_op3(&c->b, SpvOpSelect, c->t_u32, v, uconst(c, ~0u), uconst(c, 0));
    }
    case SV_SAMPLE_INDEX: {
        spv_capability(&c->b, SpvCapabilitySampleRateShading);
        uint32_t v = spv_load(&c->b, c->t_i32, builtin(c, SpvBuiltInSampleId, SpvStorageClassInput, c->t_i32));
        return spv_op1(&c->b, SpvOpBitcast, c->t_u32, v);
    }
    case SV_PRIMITIVE_ID: {
        if (c->program == DXBC_PIXEL)
            spv_capability(&c->b, SpvCapabilityGeometry);
        uint32_t v = spv_load(&c->b, c->t_i32, builtin(c, SpvBuiltInPrimitiveId, SpvStorageClassInput, c->t_i32));
        return spv_op1(&c->b, SpvOpBitcast, c->t_u32, v);
    }
    case SV_RENDER_TARGET_ARRAY_INDEX: {
        spv_capability(&c->b, SpvCapabilityGeometry);
        uint32_t v = spv_load(&c->b, c->t_i32, builtin(c, SpvBuiltInLayer, SpvStorageClassInput, c->t_i32));
        return spv_op1(&c->b, SpvOpBitcast, c->t_u32, v);
    }
    case SV_VIEWPORT_ARRAY_INDEX: {
        spv_capability(&c->b, SpvCapabilityMultiViewport);
        uint32_t v = spv_load(&c->b, c->t_i32, builtin(c, SpvBuiltInViewportIndex, SpvStorageClassInput, c->t_i32));
        return spv_op1(&c->b, SpvOpBitcast, c->t_u32, v);
    }
    default:
        fail(c, "input system value not implemented");
        return uconst(c, 0);
    }
}

/* the signature element of a register's component, for its component type. */
static const DxbcSignatureElement *signature_element(const DxbcSignature *sig, uint32_t reg, uint8_t mask)
{
    for (uint32_t i = 0; i < sig->count; i++)
        if (sig->elements[i].reg == reg && (sig->elements[i].mask & mask))
            return &sig->elements[i];
    return NULL;
}

static Kind component_kind(uint32_t type) { return type == 1 ? K_UINT : type == 2 ? K_INT : K_FLOAT; }

/* the input variables of the declared registers, read into the private array. */
static void load_inputs(Compiler *c)
{
    uint32_t v4 = c->t_vec[K_FLOAT][4];
    uint32_t fptr = spv_type_pointer(&c->b, SpvStorageClassPrivate, c->t_f32);
    uint32_t vptr = spv_type_pointer(&c->b, SpvStorageClassPrivate, v4);
    for (uint32_t reg = 0; reg < c->input_count; reg++) {
        InputDecl *d = &c->input_decl[reg];
        if (!d->declared)
            continue;
        uint8_t plain = d->mask;
        /* system values fill their components; a register may also hold plain ones. */
        if (d->sv != SV_UNDEFINED) {
            Kind kind;
            uint32_t value = system_input(c, d->sv, &kind);
            uint32_t index = uconst(c, reg);
            uint32_t base = spv_access(&c->b, vptr, c->inputs, &index, 1);
            if (d->sv == SV_POSITION) {
                spv_store(&c->b, base, value);
            } else {
                value = bitcast(c, value, kind, K_FLOAT, 1);
                for (uint32_t i = 0; i < 4; i++)
                    if (d->sv_mask & (1 << i)) {
                        uint32_t ci = uconst(c, i);
                        spv_store(&c->b, spv_access(&c->b, fptr, base, &ci, 1), value);
                    }
            }
            if (!plain)
                continue;
        }
        if (!plain)
            continue;
        /* the vertex shader's inputs take the type of their attribute's format. */
        Kind kind = K_FLOAT;
        if (c->program == DXBC_VERTEX) {
            const DxbcSignatureElement *e = signature_element(&c->info->inputs, reg, plain);
            if (e)
                kind = component_kind(e->component_type);
        }
        uint32_t type = vec_type(c, kind, 4);
        uint32_t var = global(c, SpvStorageClassInput, type, 0);
        decorate1(c, var, SpvDecorationLocation, reg);
        if (c->program == DXBC_PIXEL) {
            switch (d->interpolation) {
            case INTERP_CONSTANT:
                spv_decorate(&c->b, var, SpvDecorationFlat, NULL, 0);
                break;
            case INTERP_LINEAR_CENTROID:
                spv_decorate(&c->b, var, SpvDecorationCentroid, NULL, 0);
                break;
            case INTERP_LINEAR_NOPERSPECTIVE:
                spv_decorate(&c->b, var, SpvDecorationNoPerspective, NULL, 0);
                break;
            case INTERP_LINEAR_NOPERSPECTIVE_CENTROID:
                spv_decorate(&c->b, var, SpvDecorationNoPerspective, NULL, 0);
                spv_decorate(&c->b, var, SpvDecorationCentroid, NULL, 0);
                break;
            case INTERP_LINEAR_SAMPLE:
                spv_capability(&c->b, SpvCapabilitySampleRateShading);
                spv_decorate(&c->b, var, SpvDecorationSample, NULL, 0);
                break;
            case INTERP_LINEAR_NOPERSPECTIVE_SAMPLE:
                spv_capability(&c->b, SpvCapabilitySampleRateShading);
                spv_decorate(&c->b, var, SpvDecorationNoPerspective, NULL, 0);
                spv_decorate(&c->b, var, SpvDecorationSample, NULL, 0);
                break;
            default:
                break;
            }
        }
        d->var = var;
        uint32_t value = bitcast(c, spv_load(&c->b, type, var), kind, K_FLOAT, 4);
        uint32_t index = uconst(c, reg);
        uint32_t base = spv_access(&c->b, vptr, c->inputs, &index, 1);
        if (plain == 0xF) {
            spv_store(&c->b, base, value);
        } else {
            for (uint32_t i = 0; i < 4; i++)
                if (plain & (1 << i)) {
                    uint32_t ci = uconst(c, i);
                    spv_store(&c->b, spv_access(&c->b, fptr, base, &ci, 1), extract(c, c->t_f32, value, i));
                }
        }
    }
}

/* the clip or cull distance array of the outputs, its size from the signature. */
static uint32_t distance_count(Compiler *c, uint32_t sv)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < c->info->outputs.count; i++)
        if (c->info->outputs.elements[i].system_value == sv)
            n += popcount(c->info->outputs.elements[i].mask);
    return n;
}

/* the first distance index of a register's components, in signature order. */
static uint32_t distance_base(Compiler *c, uint32_t sv, uint32_t reg)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < c->info->outputs.count; i++) {
        const DxbcSignatureElement *e = &c->info->outputs.elements[i];
        if (e->system_value != sv)
            continue;
        if (e->reg == reg)
            return n;
        n += popcount(e->mask);
    }
    return n;
}

/* copies the private outputs to the interface. */
static void store_outputs(Compiler *c)
{
    uint32_t v4 = c->t_vec[K_FLOAT][4];
    uint32_t vptr = spv_type_pointer(&c->b, SpvStorageClassPrivate, v4);
    for (uint32_t reg = 0; reg < c->output_count; reg++) {
        OutputDecl *d = &c->output_decl[reg];
        if (!d->declared)
            continue;
        uint32_t index = uconst(c, reg);
        uint32_t value = spv_load(&c->b, v4, spv_access(&c->b, vptr, c->outputs, &index, 1));
        switch (d->sv) {
        case SV_POSITION:
            spv_store(&c->b, builtin(c, SpvBuiltInPosition, SpvStorageClassOutput, v4), value);
            break;
        case SV_CLIP_DISTANCE:
        case SV_CULL_DISTANCE: {
            uint32_t count = distance_count(c, d->sv);
            uint32_t which = d->sv == SV_CLIP_DISTANCE ? SpvBuiltInClipDistance : SpvBuiltInCullDistance;
            spv_capability(&c->b, d->sv == SV_CLIP_DISTANCE ? SpvCapabilityClipDistance : SpvCapabilityCullDistance);
            uint32_t var = builtin(c, which, SpvStorageClassOutput, spv_type_array(&c->b, c->t_f32, count ? count : 1));
            uint32_t base = distance_base(c, d->sv, reg);
            uint32_t fptr = spv_type_pointer(&c->b, SpvStorageClassOutput, c->t_f32);
            for (uint32_t i = 0, k = 0; i < 4; i++)
                if (d->mask & (1 << i)) {
                    uint32_t at = uconst(c, base + k++);
                    spv_store(&c->b, spv_access(&c->b, fptr, var, &at, 1), extract(c, c->t_f32, value, i));
                }
            break;
        }
        case SV_RENDER_TARGET_ARRAY_INDEX:
        case SV_VIEWPORT_ARRAY_INDEX: {
            uint32_t which = d->sv == SV_RENDER_TARGET_ARRAY_INDEX ? SpvBuiltInLayer : SpvBuiltInViewportIndex;
            if (c->program == DXBC_VERTEX || c->program == DXBC_DOMAIN)
                spv_capability(&c->b, d->sv == SV_RENDER_TARGET_ARRAY_INDEX ? SpvCapabilityShaderLayer
                                                                            : SpvCapabilityShaderViewportIndex);
            else
                spv_capability(&c->b, SpvCapabilityGeometry);
            uint32_t var = builtin(c, which, SpvStorageClassOutput, c->t_i32);
            uint32_t comp = __builtin_ctz(d->mask ? d->mask : 1);
            spv_store(&c->b, var, spv_op1(&c->b, SpvOpBitcast, c->t_i32, extract(c, c->t_f32, value, comp)));
            break;
        }
        default: {
            if (!d->var) {
                Kind kind = K_FLOAT;
                if (c->program == DXBC_PIXEL) {
                    const DxbcSignatureElement *e = signature_element(&c->info->outputs, reg, d->mask);
                    if (e)
                        kind = component_kind(e->component_type);
                }
                d->var = global(c, SpvStorageClassOutput, vec_type(c, kind, 4), 0);
                decorate1(c, d->var, SpvDecorationLocation, reg);
                d->kind = kind;
            }
            spv_store(&c->b, d->var, bitcast(c, value, K_FLOAT, d->kind, 4));
            break;
        }
        }
    }
    if (c->depth_output) {
        uint32_t var = builtin(c, SpvBuiltInFragDepth, SpvStorageClassOutput, c->t_f32);
        spv_store(&c->b, var, spv_load(&c->b, c->t_f32, c->depth_output));
    }
    if (c->coverage_output) {
        uint32_t arr = spv_type_array(&c->b, c->t_i32, 1);
        uint32_t var = builtin(c, SpvBuiltInSampleMask, SpvStorageClassOutput, arr);
        uint32_t zero = iconst(c, 0);
        uint32_t ptr = spv_access(&c->b, spv_type_pointer(&c->b, SpvStorageClassOutput, c->t_i32), var, &zero, 1);
        spv_store(&c->b, ptr, spv_op1(&c->b, SpvOpBitcast, c->t_i32, spv_load(&c->b, c->t_u32, c->coverage_output)));
    }
}

static void store_output_special(Compiler *c, const DxbcOperand *op, uint32_t value, Kind k)
{
    if (op->type == OPERAND_OUTPUT_COVERAGE_MASK) {
        if (!c->coverage_output)
            c->coverage_output = global(c, SpvStorageClassPrivate, c->t_u32, 0);
        spv_store(&c->b, c->coverage_output, bitcast(c, value, k, K_UINT, 1));
        return;
    }
    if (!c->depth_output)
        c->depth_output = global(c, SpvStorageClassPrivate, c->t_f32, 0);
    spv_store(&c->b, c->depth_output, bitcast(c, value, k, K_FLOAT, 1));
}

/* ------------------------------------------------------------------ declarations */

static Kind return_kind(uint32_t token)
{
    switch (token & 0xF) {
    case RET_SINT:
        return K_INT;
    case RET_UINT:
        return K_UINT;
    default:
        return K_FLOAT;
    }
}

/* reads the declarations, before any code is made, so registers and resources are known. */
static int prepass(Compiler *c)
{
    DxbcReader r = {c->code, c->code_count, 0};
    DxbcInstruction ins;
    int res;
    while ((res = dxbc_read(&r, &ins)) > 0) {
        const DxbcOperand *op = &ins.operands[0];
        const uint32_t *x = ins.extra;
        switch (ins.opcode) {
        case OP_DCL_TEMPS:
            if (!ins.extra_count || x[0] > MAX_TEMPS)
                return fail(c, "too many temporaries");
            c->temp_count = x[0];
            break;
        case OP_DCL_INDEXABLE_TEMP:
            if (ins.extra_count < 3 || x[0] >= 256)
                return fail(c, "bad indexable temporary");
            c->indexable_size[x[0]] = x[1];
            break;
        case OP_DCL_GLOBAL_FLAGS:
            c->global_flags = ins.token >> 11;
            break;
        case OP_DCL_CONSTANT_BUFFER:
            if (op->index[0].immediate < 14)
                c->info->cb_mask |= 1u << op->index[0].immediate;
            break;
        case OP_DCL_SAMPLER:
            if (op->index[0].immediate < 16) {
                c->info->sampler_mask |= 1u << op->index[0].immediate;
                c->sampler_mode[op->index[0].immediate] = TOK_SAMPLER_MODE(ins.token);
            }
            break;
        case OP_DCL_RESOURCE:
        case OP_DCL_RESOURCE_RAW:
        case OP_DCL_RESOURCE_STRUCTURED: {
            uint32_t slot = op->index[0].immediate;
            if (slot >= 128)
                return fail(c, "resource slot out of range");
            Resource *s = &c->srv[slot];
            s->declared = 1;
            s->dim = ins.opcode == OP_DCL_RESOURCE ? TOK_RESOURCE_DIMENSION(ins.token)
                     : ins.opcode == OP_DCL_RESOURCE_RAW ? RES_RAW_BUFFER
                                                         : RES_STRUCTURED_BUFFER;
            s->raw = ins.opcode == OP_DCL_RESOURCE_RAW;
            s->structured = ins.opcode == OP_DCL_RESOURCE_STRUCTURED;
            s->stride = s->structured && ins.extra_count ? x[0] : 0;
            s->kind = ins.opcode == OP_DCL_RESOURCE && ins.extra_count ? return_kind(x[0]) : K_UINT;
            c->info->srv_mask[slot / 32] |= 1u << (slot % 32);
            c->info->srv_kind[slot] = s->dim == RES_BUFFER || s->raw || s->structured ? DXBC_SLOT_BUFFER : DXBC_SLOT_IMAGE;
            break;
        }
        case OP_DCL_UAV_TYPED:
        case OP_DCL_UAV_RAW:
        case OP_DCL_UAV_STRUCTURED: {
            uint32_t slot = op->index[0].immediate;
            if (slot >= 64)
                return fail(c, "unordered access view slot out of range");
            Resource *u = &c->uav[slot];
            u->declared = 1;
            u->dim = ins.opcode == OP_DCL_UAV_TYPED ? TOK_RESOURCE_DIMENSION(ins.token)
                     : ins.opcode == OP_DCL_UAV_RAW ? RES_RAW_BUFFER
                                                    : RES_STRUCTURED_BUFFER;
            u->raw = ins.opcode == OP_DCL_UAV_RAW;
            u->structured = ins.opcode == OP_DCL_UAV_STRUCTURED;
            u->stride = u->structured && ins.extra_count ? x[0] : 0;
            u->kind = ins.opcode == OP_DCL_UAV_TYPED && ins.extra_count ? return_kind(x[0]) : K_UINT;
            u->counter = TOK_UAV_COUNTER(ins.token);
            c->info->uav_mask |= 1ull << slot;
            if (u->counter)
                c->info->uav_counter_mask |= 1ull << slot;
            c->info->uav_kind[slot] = u->dim == RES_BUFFER || u->raw || u->structured ? DXBC_SLOT_BUFFER : DXBC_SLOT_IMAGE;
            break;
        }
        case OP_DCL_INPUT:
        case OP_DCL_INPUT_SGV:
        case OP_DCL_INPUT_SIV:
        case OP_DCL_INPUT_PS:
        case OP_DCL_INPUT_PS_SGV:
        case OP_DCL_INPUT_PS_SIV: {
            if (op->type != OPERAND_INPUT) {
                /* system value operands, read where they are used. */
                break;
            }
            uint32_t reg = op->index[op->index_count ? op->index_count - 1 : 0].immediate;
            if (reg >= MAX_REGS)
                return fail(c, "input register out of range");
            InputDecl *d = &c->input_decl[reg];
            d->declared = 1;
            uint32_t sv = SV_UNDEFINED;
            if (ins.opcode == OP_DCL_INPUT_SGV || ins.opcode == OP_DCL_INPUT_SIV || ins.opcode == OP_DCL_INPUT_PS_SGV ||
                ins.opcode == OP_DCL_INPUT_PS_SIV)
                sv = ins.extra_count ? x[0] : SV_UNDEFINED;
            if (sv != SV_UNDEFINED && sv != SV_CLIP_DISTANCE && sv != SV_CULL_DISTANCE) {
                d->sv = sv;
                d->sv_mask |= op->mask;
            } else {
                d->mask |= op->mask;
            }
            if (ins.opcode == OP_DCL_INPUT_PS || ins.opcode == OP_DCL_INPUT_PS_SIV)
                d->interpolation = TOK_INTERPOLATION(ins.token);
            if (ins.opcode == OP_DCL_INPUT_PS_SGV)
                d->interpolation = INTERP_CONSTANT;
            if (reg + 1 > c->input_count)
                c->input_count = reg + 1;
            c->info->input_mask |= 1u << (reg & 31);
            break;
        }
        case OP_DCL_OUTPUT:
        case OP_DCL_OUTPUT_SGV:
        case OP_DCL_OUTPUT_SIV: {
            if (op->type != OPERAND_OUTPUT) {
                if (op->type == OPERAND_OUTPUT_DEPTH || op->type == OPERAND_OUTPUT_DEPTH_GREATER_EQUAL ||
                    op->type == OPERAND_OUTPUT_DEPTH_LESS_EQUAL)
                    c->info->writes_depth = (int)op->type;
                break;
            }
            uint32_t reg = op->index[0].immediate;
            if (reg >= MAX_REGS)
                return fail(c, "output register out of range");
            OutputDecl *d = &c->output_decl[reg];
            d->declared = 1;
            d->mask |= op->mask;
            if (ins.opcode != OP_DCL_OUTPUT && ins.extra_count)
                d->sv = x[0];
            if (reg + 1 > c->output_count)
                c->output_count = reg + 1;
            c->info->output_mask |= 1u << (reg & 31);
            break;
        }
        case OP_CUSTOMDATA:
            if (TOK_CUSTOMDATA_CLASS(ins.token) == 3 && !c->icb) {
                uint32_t count = ins.extra_count / 4;
                uint32_t parts[4], *elements = mime_alloc(c->alloc, (count ? count : 1) * sizeof(uint32_t));
                if (!elements)
                    return fail(c, "out of memory");
                for (uint32_t i = 0; i < count; i++) {
                    for (int k = 0; k < 4; k++)
                        parts[k] = uconst(c, ins.extra[i * 4 + k]);
                    elements[i] = spv_const_composite(&c->b, c->t_vec[K_UINT][4], parts, 4);
                }
                uint32_t type = spv_type_array(&c->b, c->t_vec[K_UINT][4], count ? count : 1);
                uint32_t init = count ? spv_const_composite_array(&c->b, type, elements, count) : spv_const_null(&c->b, type);
                mime_free(c->alloc, elements);
                c->icb = global(c, SpvStorageClassPrivate, type, init);
                c->icb_size = count;
            }
            break;
        case OP_DCL_THREAD_GROUP:
            if (ins.extra_count >= 3) {
                c->thread_group[0] = x[0], c->thread_group[1] = x[1], c->thread_group[2] = x[2];
                c->info->thread_group[0] = x[0], c->info->thread_group[1] = x[1], c->info->thread_group[2] = x[2];
            }
            break;
        case OP_DCL_TGSM_RAW:
        case OP_DCL_TGSM_STRUCTURED: {
            uint32_t reg = op->index[0].immediate;
            if (reg >= 256 || !ins.extra_count)
                return fail(c, "bad shared memory declaration");
            uint32_t words = ins.opcode == OP_DCL_TGSM_RAW ? x[0] / 4 : (ins.extra_count >= 2 ? x[0] * x[1] / 4 : 0);
            c->tgsm_size[reg] = words ? words : 1;
            c->tgsm_stride[reg] = ins.opcode == OP_DCL_TGSM_STRUCTURED ? x[0] : 0;
            break;
        }
        case OP_LABEL:
            if (c->label_count >= MAX_LABELS)
                return fail(c, "too many subroutines");
            c->label_number[c->label_count] = op->index[0].immediate;
            c->label_at[c->label_count] = (uint32_t)(ins.words - c->code);
            c->label_fn[c->label_count] = spv_id(&c->b);
            c->label_count++;
            break;
        case OP_DCL_GS_INPUT_PRIMITIVE:
        case OP_DCL_GS_OUTPUT_TOPOLOGY:
        case OP_DCL_MAX_OUTPUT_VERTEX_COUNT:
        case OP_HS_DECLS:
        case OP_DCL_TESS_DOMAIN:
            return fail(c, "geometry and tessellation shaders are not implemented");
        case OP_DCL_INTERFACE:
        case OP_DCL_FUNCTION_BODY:
        case OP_DCL_FUNCTION_TABLE:
            return fail(c, "class linkage is not implemented");
        default:
            break;
        }
    }
    return res < 0 ? fail(c, "malformed bytecode") : 0;
}

/* the globals of the registers the declarations named. */
static void make_registers(Compiler *c)
{
    uint32_t v4 = c->t_vec[K_FLOAT][4];
    for (uint32_t i = 0; i < c->temp_count; i++)
        c->temps[i] = global(c, SpvStorageClassPrivate, v4, 0);
    for (uint32_t i = 0; i < 256; i++)
        if (c->indexable_size[i])
            c->indexable[i] = global(c, SpvStorageClassPrivate, spv_type_array(&c->b, v4, c->indexable_size[i]), 0);
    if (c->input_count)
        c->inputs = global(c, SpvStorageClassPrivate, spv_type_array(&c->b, v4, c->input_count), 0);
    if (c->output_count)
        c->outputs = global(c, SpvStorageClassPrivate, spv_type_array(&c->b, v4, c->output_count), 0);
    for (uint32_t i = 0; i < 256; i++)
        if (c->tgsm_size[i])
            c->tgsm[i] = global(c, SpvStorageClassWorkgroup, spv_type_array(&c->b, c->t_u32, c->tgsm_size[i]), 0);
    /* the stage's table. */
    uint32_t array = spv_type_array_unique(&c->b, c->t_vec[K_UINT][4], DXBC_TABLE_DWORDS / 4);
    decorate1(c, array, SpvDecorationArrayStride, 16);
    uint32_t block = spv_type_struct_unique(&c->b, &array, 1);
    uint32_t offset = 0;
    spv_member_decorate(&c->b, block, 0, SpvDecorationOffset, &offset, 1);
    spv_decorate(&c->b, block, SpvDecorationBlock, NULL, 0);
    c->table = global(c, SpvStorageClassUniform, block, 0);
    decorate1(c, c->table, SpvDecorationDescriptorSet, DXBC_SET_TABLE);
    decorate1(c, c->table, SpvDecorationBinding, c->binding);
}

/* ------------------------------------------------------------------ arithmetic */

static uint8_t dst_mask(const DxbcOperand *op) { return op->mask ? op->mask : 1; }

static uint32_t binary(Compiler *c, const DxbcInstruction *ins, uint32_t op, Kind k, uint8_t mask, uint32_t a,
                       uint32_t b)
{
    uint32_t r = spv_op2(&c->b, op, vec_type(c, k, popcount(mask)), a, b);
    decorate_precise(c, ins, r);
    return r;
}

/* the destination gets op of its two sources, all of the kind. */
static void alu2(Compiler *c, const DxbcInstruction *ins, uint32_t op, Kind k)
{
    uint8_t mask = dst_mask(&ins->operands[0]);
    uint32_t a = load(c, &ins->operands[1], mask, k), b = load(c, &ins->operands[2], mask, k);
    store(c, ins, &ins->operands[0], binary(c, ins, op, k, mask, a, b), k);
}

static void alu1(Compiler *c, const DxbcInstruction *ins, uint32_t op, Kind k, Kind out)
{
    uint8_t mask = dst_mask(&ins->operands[0]);
    uint32_t a = load(c, &ins->operands[1], mask, k);
    uint32_t r = spv_op1(&c->b, op, vec_type(c, out, popcount(mask)), a);
    decorate_precise(c, ins, r);
    store(c, ins, &ins->operands[0], r, out);
}

static void glsl1(Compiler *c, const DxbcInstruction *ins, uint32_t instruction, Kind k)
{
    uint8_t mask = dst_mask(&ins->operands[0]);
    uint32_t a = load(c, &ins->operands[1], mask, k);
    uint32_t r = spv_glsl(&c->b, vec_type(c, k, popcount(mask)), instruction, &a, 1);
    decorate_precise(c, ins, r);
    store(c, ins, &ins->operands[0], r, k);
}

static void glsl2(Compiler *c, const DxbcInstruction *ins, uint32_t instruction, Kind k)
{
    uint8_t mask = dst_mask(&ins->operands[0]);
    uint32_t args[2] = {load(c, &ins->operands[1], mask, k), load(c, &ins->operands[2], mask, k)};
    uint32_t r = spv_glsl(&c->b, vec_type(c, k, popcount(mask)), instruction, args, 2);
    decorate_precise(c, ins, r);
    store(c, ins, &ins->operands[0], r, k);
}

static void compare(Compiler *c, const DxbcInstruction *ins, uint32_t op, Kind k)
{
    uint8_t mask = dst_mask(&ins->operands[0]);
    uint32_t n = popcount(mask);
    uint32_t a = load(c, &ins->operands[1], mask, k), b = load(c, &ins->operands[2], mask, k);
    uint32_t r = spv_op2(&c->b, op, c->t_bvec[n], a, b);
    store(c, ins, &ins->operands[0], bool_to_mask(c, r, n), K_UINT);
}

static void dot(Compiler *c, const DxbcInstruction *ins, uint32_t n)
{
    uint32_t a = load_first(c, &ins->operands[1], n, K_FLOAT), b = load_first(c, &ins->operands[2], n, K_FLOAT);
    uint32_t r;
    if (n == 1)
        r = spv_op2(&c->b, SpvOpFMul, c->t_f32, a, b);
    else
        r = spv_op2(&c->b, SpvOpDot, c->t_f32, a, b);
    decorate_precise(c, ins, r);
    store(c, ins, &ins->operands[0], splat(c, K_FLOAT, popcount(dst_mask(&ins->operands[0])), r), K_FLOAT);
}

/* shifts take the low 5 bits of their amount, as direct3d's do. */
static void shift(Compiler *c, const DxbcInstruction *ins, uint32_t op, Kind k)
{
    uint8_t mask = dst_mask(&ins->operands[0]);
    uint32_t n = popcount(mask);
    uint32_t a = load(c, &ins->operands[1], mask, k);
    uint32_t amount = load(c, &ins->operands[2], mask, K_UINT);
    amount = spv_op2(&c->b, SpvOpBitwiseAnd, vec_type(c, K_UINT, n), amount, splat_const(c, K_UINT, n, 31));
    store(c, ins, &ins->operands[0], spv_op2(&c->b, op, vec_type(c, k, n), a, amount), k);
}

/* the field width and offset of a bit field component, the width clipped to the bits left. */
static void bit_field(Compiler *c, uint32_t *width, uint32_t *offset)
{
    uint32_t mask = uconst(c, 31);
    *width = spv_op2(&c->b, SpvOpBitwiseAnd, c->t_u32, *width, mask);
    *offset = spv_op2(&c->b, SpvOpBitwiseAnd, c->t_u32, *offset, mask);
    uint32_t left = spv_op2(&c->b, SpvOpISub, c->t_u32, uconst(c, 32), *offset);
    uint32_t args[2] = {*width, left};
    *width = spv_glsl(&c->b, c->t_u32, GLSLstd450UMin, args, 2);
}

/* spir-v takes the offset and count of bit fields as scalars: one component at a time. */
static void bfe(Compiler *c, const DxbcInstruction *ins, Kind k)
{
    uint8_t mask = dst_mask(&ins->operands[0]);
    uint32_t n = popcount(mask), parts[4], j = 0;
    for (uint32_t i = 0; i < 4; i++) {
        if (!(mask & (1 << i)))
            continue;
        uint32_t width = load_scalar(c, &ins->operands[1], ins->operands[1].swizzle[i], K_UINT);
        uint32_t offset = load_scalar(c, &ins->operands[2], ins->operands[2].swizzle[i], K_UINT);
        uint32_t value = load_scalar(c, &ins->operands[3], ins->operands[3].swizzle[i], k);
        bit_field(c, &width, &offset);
        parts[j++] = spv_op3(&c->b, k == K_INT ? SpvOpBitFieldSExtract : SpvOpBitFieldUExtract, scalar_type(c, k),
                             value, offset, width);
    }
    uint32_t r = n == 1 ? parts[0] : spv_op(&c->b, SpvOpCompositeConstruct, vec_type(c, k, n), parts, n);
    store(c, ins, &ins->operands[0], r, k);
}

static void bfi(Compiler *c, const DxbcInstruction *ins)
{
    uint8_t mask = dst_mask(&ins->operands[0]);
    uint32_t n = popcount(mask), parts[4], j = 0;
    for (uint32_t i = 0; i < 4; i++) {
        if (!(mask & (1 << i)))
            continue;
        uint32_t width = load_scalar(c, &ins->operands[1], ins->operands[1].swizzle[i], K_UINT);
        uint32_t offset = load_scalar(c, &ins->operands[2], ins->operands[2].swizzle[i], K_UINT);
        uint32_t insert = load_scalar(c, &ins->operands[3], ins->operands[3].swizzle[i], K_UINT);
        uint32_t base = load_scalar(c, &ins->operands[4], ins->operands[4].swizzle[i], K_UINT);
        bit_field(c, &width, &offset);
        uint32_t w[4] = {base, insert, offset, width};
        parts[j++] = spv_op(&c->b, SpvOpBitFieldInsert, c->t_u32, w, 4);
    }
    uint32_t r = n == 1 ? parts[0] : spv_op(&c->b, SpvOpCompositeConstruct, vec_type(c, K_UINT, n), parts, n);
    store(c, ins, &ins->operands[0], r, K_UINT);
}

/* firstbit_hi counts from the top bit, all ones when no bit is set. */
static void firstbit(Compiler *c, const DxbcInstruction *ins, uint32_t instruction)
{
    uint8_t mask = dst_mask(&ins->operands[0]);
    uint32_t n = popcount(mask);
    Kind k = instruction == GLSLstd450FindSMsb ? K_INT : K_UINT;
    uint32_t value = load(c, &ins->operands[1], mask, k);
    uint32_t u = vec_type(c, K_UINT, n);
    uint32_t r = bitcast(c, spv_glsl(&c->b, vec_type(c, k, n), instruction, &value, 1), k, K_UINT, n);
    if (instruction != GLSLstd450FindILsb) {
        uint32_t none = spv_op2(&c->b, SpvOpIEqual, c->t_bvec[n], r, splat_const(c, K_UINT, n, ~0u));
        uint32_t flipped = spv_op2(&c->b, SpvOpISub, u, splat_const(c, K_UINT, n, 31), r);
        r = spv_op3(&c->b, SpvOpSelect, u, none, r, flipped);
    }
    store(c, ins, &ins->operands[0], r, K_UINT);
}

/* the two results of a wide multiply, carry or borrow: high or carry first, as dxbc orders them. */
static void wide(Compiler *c, const DxbcInstruction *ins, uint32_t op, Kind k, int low_second)
{
    const DxbcOperand *d0 = &ins->operands[0], *d1 = &ins->operands[1];
    uint8_t mask = d0->type != OPERAND_NULL ? dst_mask(d0) : dst_mask(d1);
    uint32_t n = popcount(mask);
    uint32_t a = load(c, &ins->operands[2], mask, k), b = load(c, &ins->operands[3], mask, k);
    uint32_t type = vec_type(c, k, n);
    uint32_t members[2] = {type, type};
    uint32_t pair = spv_op2(&c->b, op, spv_type_struct(&c->b, members, 2), a, b);
    uint32_t first = extract(c, type, pair, 0), second = extract(c, type, pair, 1);
    /* the multiplies give the low half first, dxbc wants the high half first. */
    if (low_second) {
        store(c, ins, d0, second, k);
        store(c, ins, d1, first, k);
    } else {
        store(c, ins, d0, first, k);
        store(c, ins, d1, second, k);
    }
}

/* udiv: quotient and remainder, all ones for both on division by zero. */
static void udiv(Compiler *c, const DxbcInstruction *ins)
{
    const DxbcOperand *d0 = &ins->operands[0], *d1 = &ins->operands[1];
    for (int which = 0; which < 2; which++) {
        const DxbcOperand *d = which ? d1 : d0;
        if (d->type == OPERAND_NULL)
            continue;
        uint8_t mask = dst_mask(d);
        uint32_t n = popcount(mask);
        uint32_t u = vec_type(c, K_UINT, n);
        uint32_t a = load(c, &ins->operands[2], mask, K_UINT), b = load(c, &ins->operands[3], mask, K_UINT);
        uint32_t zero = spv_op2(&c->b, SpvOpIEqual, c->t_bvec[n], b, splat_const(c, K_UINT, n, 0));
        uint32_t safe = spv_op3(&c->b, SpvOpSelect, u, zero, splat_const(c, K_UINT, n, 1), b);
        uint32_t r = spv_op2(&c->b, which ? SpvOpUMod : SpvOpUDiv, u, a, safe);
        r = spv_op3(&c->b, SpvOpSelect, u, zero, splat_const(c, K_UINT, n, ~0u), r);
        store(c, ins, d, r, K_UINT);
    }
}

static void sin_cos(Compiler *c, const DxbcInstruction *ins)
{
    for (int which = 0; which < 2; which++) {
        const DxbcOperand *d = &ins->operands[which];
        if (d->type == OPERAND_NULL)
            continue;
        uint8_t mask = dst_mask(d);
        uint32_t a = load(c, &ins->operands[2], mask, K_FLOAT);
        uint32_t r = spv_glsl(&c->b, vec_type(c, K_FLOAT, popcount(mask)), which ? GLSLstd450Cos : GLSLstd450Sin, &a, 1);
        store(c, ins, d, r, K_FLOAT);
    }
}

static void movc(Compiler *c, const DxbcInstruction *ins)
{
    uint8_t mask = dst_mask(&ins->operands[0]);
    uint32_t n = popcount(mask);
    uint32_t cond = nonzero(c, load(c, &ins->operands[1], mask, K_UINT), n);
    uint32_t a = load(c, &ins->operands[2], mask, K_FLOAT), b = load(c, &ins->operands[3], mask, K_FLOAT);
    store(c, ins, &ins->operands[0], spv_op3(&c->b, SpvOpSelect, vec_type(c, K_FLOAT, n), cond, a, b), K_FLOAT);
}

static void swapc(Compiler *c, const DxbcInstruction *ins)
{
    for (int which = 0; which < 2; which++) {
        const DxbcOperand *d = &ins->operands[which];
        if (d->type == OPERAND_NULL)
            continue;
        uint8_t mask = dst_mask(d);
        uint32_t n = popcount(mask);
        uint32_t cond = nonzero(c, load(c, &ins->operands[2], mask, K_UINT), n);
        uint32_t a = load(c, &ins->operands[3], mask, K_FLOAT), b = load(c, &ins->operands[4], mask, K_FLOAT);
        uint32_t r = spv_op3(&c->b, SpvOpSelect, vec_type(c, K_FLOAT, n), cond, which ? a : b, which ? b : a);
        store(c, ins, d, r, K_FLOAT);
    }
}

static void mad(Compiler *c, const DxbcInstruction *ins, Kind k)
{
    uint8_t mask = dst_mask(&ins->operands[0]);
    uint32_t a = load(c, &ins->operands[1], mask, k), b = load(c, &ins->operands[2], mask, k);
    uint32_t d = load(c, &ins->operands[3], mask, k);
    uint32_t m = binary(c, ins, k == K_FLOAT ? SpvOpFMul : SpvOpIMul, k, mask, a, b);
    store(c, ins, &ins->operands[0], binary(c, ins, k == K_FLOAT ? SpvOpFAdd : SpvOpIAdd, k, mask, m, d), k);
}

static void rcp(Compiler *c, const DxbcInstruction *ins)
{
    uint8_t mask = dst_mask(&ins->operands[0]);
    uint32_t n = popcount(mask);
    uint32_t a = load(c, &ins->operands[1], mask, K_FLOAT);
    store(c, ins, &ins->operands[0], binary(c, ins, SpvOpFDiv, K_FLOAT, mask, splat_const(c, K_FLOAT, n, 0x3F800000), a),
          K_FLOAT);
}

/* converting halves: the low 16 bits of each component. */
static void half(Compiler *c, const DxbcInstruction *ins, int to_half)
{
    uint8_t mask = dst_mask(&ins->operands[0]);
    uint32_t n = popcount(mask);
    uint32_t v2 = c->t_vec[K_FLOAT][2];
    uint32_t parts[4];
    for (uint32_t i = 0, j = 0; i < 4; i++) {
        if (!(mask & (1 << i)))
            continue;
        DxbcOperand single = ins->operands[1];
        single.swizzle[0] = ins->operands[1].swizzle[i];
        if (to_half) {
            uint32_t x = load(c, &single, 1, K_FLOAT);
            uint32_t pair[2] = {x, fconst(c, 0.0f)};
            uint32_t v = spv_op(&c->b, SpvOpCompositeConstruct, v2, pair, 2);
            parts[j++] = spv_glsl(&c->b, c->t_u32, GLSLstd450PackHalf2x16, &v, 1);
        } else {
            uint32_t x = load(c, &single, 1, K_UINT);
            x = spv_op2(&c->b, SpvOpBitwiseAnd, c->t_u32, x, uconst(c, 0xFFFF));
            uint32_t v = spv_glsl(&c->b, v2, GLSLstd450UnpackHalf2x16, &x, 1);
            parts[j++] = spv_op1(&c->b, SpvOpBitcast, c->t_u32, extract(c, c->t_f32, v, 0));
        }
    }
    uint32_t r = n == 1 ? parts[0] : spv_op(&c->b, SpvOpCompositeConstruct, vec_type(c, K_UINT, n), parts, n);
    store(c, ins, &ins->operands[0], r, K_UINT);
}

/* ------------------------------------------------------------------ textures */

/* the coordinates a dimension samples with, and those of its gradients and offsets. */
static uint32_t coord_count(uint32_t dim)
{
    switch (dim) {
    case RES_TEXTURE1D:
    case RES_BUFFER:
    case RES_RAW_BUFFER:
    case RES_STRUCTURED_BUFFER:
        return 1;
    case RES_TEXTURE1DARRAY:
    case RES_TEXTURE2D:
    case RES_TEXTURE2DMS:
        return 2;
    case RES_TEXTURECUBEARRAY:
        return 4;
    default:
        return 3;
    }
}

static uint32_t spatial_count(uint32_t dim)
{
    switch (dim) {
    case RES_TEXTURE1D:
    case RES_TEXTURE1DARRAY:
        return 1;
    case RES_TEXTURE3D:
    case RES_TEXTURECUBE:
    case RES_TEXTURECUBEARRAY:
        return 3;
    default:
        return 2;
    }
}

/* the immediate offsets of the instruction, as a constant, or 0 without any. */
static uint32_t const_offset(Compiler *c, const DxbcInstruction *ins, uint32_t dim)
{
    if (!ins->offset[0] && !ins->offset[1] && !ins->offset[2])
        return 0;
    uint32_t n = spatial_count(dim);
    if (dim == RES_TEXTURECUBE || dim == RES_TEXTURECUBEARRAY)
        return 0;
    uint32_t parts[3];
    for (uint32_t i = 0; i < n; i++)
        parts[i] = iconst(c, ins->offset[i]);
    return n == 1 ? parts[0] : spv_const_composite(&c->b, vec_type(c, K_INT, n), parts, n);
}

static int resource_slot(Compiler *c, const DxbcOperand *op, Resource **r)
{
    uint32_t slot = op->index[0].immediate;
    if (op->type == OPERAND_RESOURCE && slot < 128 && c->srv[slot].declared) {
        *r = &c->srv[slot];
        return (int)slot;
    }
    if (op->type == OPERAND_UNORDERED_ACCESS_VIEW && slot < 64 && c->uav[slot].declared) {
        *r = &c->uav[slot];
        return (int)slot;
    }
    fail(c, "undeclared resource");
    return -1;
}

/* stores a vec4 result of a texture operation, swizzled by the resource operand. */
static void store_texel(Compiler *c, const DxbcInstruction *ins, const DxbcOperand *resource, uint32_t vec4, Kind k)
{
    const DxbcOperand *d = &ins->operands[0];
    store(c, ins, d, pick(c, vec4, k, dst_mask(d), resource->swizzle), k);
}

static void sample(Compiler *c, const DxbcInstruction *ins)
{
    const DxbcOperand *address = &ins->operands[1], *resource = &ins->operands[2], *sampler = &ins->operands[3];
    Resource *r;
    int slot = resource_slot(c, resource, &r);
    if (slot < 0)
        return;
    int depth = ins->opcode == OP_SAMPLE_C || ins->opcode == OP_SAMPLE_C_LZ;
    uint32_t image_type;
    uint32_t image = load_srv(c, (uint32_t)slot, depth, &image_type);
    uint32_t si = spv_op2(&c->b, SpvOpSampledImage, spv_type_sampled_image(&c->b, image_type), image,
                          load_sampler(c, sampler->index[0].immediate & 15));
    uint32_t n = coord_count(r->dim);
    uint32_t coord = load_first(c, address, n, K_FLOAT);
    uint32_t w[12], count = 0, operands = 0;
    w[count++] = si;
    w[count++] = coord;
    if (depth)
        w[count++] = load_scalar(c, &ins->operands[4], ins->operands[4].swizzle[0], K_FLOAT);
    uint32_t mask_at = count++;
    int implicit = c->program == DXBC_PIXEL && ins->opcode != OP_SAMPLE_L && ins->opcode != OP_SAMPLE_D &&
                   ins->opcode != OP_SAMPLE_C_LZ;
    if (ins->opcode == OP_SAMPLE_B) {
        operands |= SpvImageOperandsBiasMask;
        w[count++] = load_scalar(c, &ins->operands[4], ins->operands[4].swizzle[0], K_FLOAT);
        if (!implicit)
            fail(c, "sample_b outside a pixel shader");
    } else if (ins->opcode == OP_SAMPLE_L) {
        operands |= SpvImageOperandsLodMask;
        w[count++] = load_scalar(c, &ins->operands[4], ins->operands[4].swizzle[0], K_FLOAT);
    } else if (ins->opcode == OP_SAMPLE_D) {
        operands |= SpvImageOperandsGradMask;
        uint32_t g = spatial_count(r->dim);
        w[count++] = load_first(c, &ins->operands[4], g, K_FLOAT);
        w[count++] = load_first(c, &ins->operands[5], g, K_FLOAT);
    } else if (!implicit) {
        operands |= SpvImageOperandsLodMask;
        w[count++] = fconst(c, 0.0f);
    }
    uint32_t offset = const_offset(c, ins, r->dim);
    if (offset) {
        operands |= SpvImageOperandsConstOffsetMask;
        w[count++] = offset;
    }
    w[mask_at] = operands;
    if (!operands) {
        for (uint32_t i = mask_at; i + 1 < count; i++)
            w[i] = w[i + 1];
        count--;
    }
    uint32_t result;
    if (depth) {
        uint32_t op = implicit ? SpvOpImageSampleDrefImplicitLod : SpvOpImageSampleDrefExplicitLod;
        result = splat(c, K_FLOAT, 4, spv_op(&c->b, op, c->t_f32, w, count));
        store_texel(c, ins, resource, result, K_FLOAT);
        return;
    }
    uint32_t op = implicit ? SpvOpImageSampleImplicitLod : SpvOpImageSampleExplicitLod;
    result = spv_op(&c->b, op, vec_type(c, r->kind, 4), w, count);
    store_texel(c, ins, resource, result, r->kind);
}

static void gather(Compiler *c, const DxbcInstruction *ins)
{
    int po = ins->opcode == OP_GATHER4_PO || ins->opcode == OP_GATHER4_PO_C;
    int cmp = ins->opcode == OP_GATHER4_C || ins->opcode == OP_GATHER4_PO_C;
    const DxbcOperand *address = &ins->operands[1];
    const DxbcOperand *resource = &ins->operands[po ? 3 : 2], *sampler = &ins->operands[po ? 4 : 3];
    Resource *r;
    int slot = resource_slot(c, resource, &r);
    if (slot < 0)
        return;
    uint32_t image_type;
    uint32_t image = load_srv(c, (uint32_t)slot, cmp, &image_type);
    uint32_t si = spv_op2(&c->b, SpvOpSampledImage, spv_type_sampled_image(&c->b, image_type), image,
                          load_sampler(c, sampler->index[0].immediate & 15));
    uint32_t w[8], count = 0;
    w[count++] = si;
    w[count++] = load_first(c, address, coord_count(r->dim), K_FLOAT);
    if (cmp)
        w[count++] = load_scalar(c, &ins->operands[po ? 5 : 4], ins->operands[po ? 5 : 4].swizzle[0], K_FLOAT);
    else
        w[count++] = uconst(c, sampler->swizzle[0]);
    if (po) {
        spv_capability(&c->b, SpvCapabilityImageGatherExtended);
        w[count++] = SpvImageOperandsOffsetMask;
        w[count++] = load_first(c, &ins->operands[2], spatial_count(r->dim), K_INT);
    } else {
        uint32_t offset = const_offset(c, ins, r->dim);
        if (offset) {
            w[count++] = SpvImageOperandsConstOffsetMask;
            w[count++] = offset;
        }
    }
    Kind k = cmp ? K_FLOAT : r->kind;
    uint32_t result = spv_op(&c->b, cmp ? SpvOpImageDrefGather : SpvOpImageGather, vec_type(c, k, 4), w, count);
    store_texel(c, ins, resource, result, k);
}

/* ld and ld_ms: texels by integer coordinates, the mip in w. */
static void fetch(Compiler *c, const DxbcInstruction *ins)
{
    const DxbcOperand *address = &ins->operands[1], *resource = &ins->operands[2];
    Resource *r;
    int slot = resource_slot(c, resource, &r);
    if (slot < 0)
        return;
    uint32_t image_type;
    uint32_t image = load_srv(c, (uint32_t)slot, 0, &image_type);
    uint32_t w[8], count = 0;
    w[count++] = image;
    if (r->dim == RES_BUFFER) {
        w[count++] = load_scalar(c, address, address->swizzle[0], K_INT);
    } else {
        w[count++] = load_first(c, address, coord_count(r->dim), K_INT);
        uint32_t mask_at = count++;
        uint32_t operands = 0;
        if (ins->opcode == OP_LD_MS) {
            operands |= SpvImageOperandsSampleMask;
            w[count++] = load_scalar(c, &ins->operands[3], ins->operands[3].swizzle[0], K_INT);
        } else {
            operands |= SpvImageOperandsLodMask;
            w[count++] = load_scalar(c, address, address->swizzle[3], K_INT);
        }
        uint32_t offset = ins->opcode == OP_LD ? const_offset(c, ins, r->dim) : 0;
        if (offset) {
            operands |= SpvImageOperandsConstOffsetMask;
            w[count++] = offset;
        }
        w[mask_at] = operands;
    }
    uint32_t result = spv_op(&c->b, SpvOpImageFetch, vec_type(c, r->kind, 4), w, count);
    store_texel(c, ins, resource, result, r->kind);
}

/* the size of a view and its mip count, as resinfo answers: floats, their reciprocals or uints. */
static void resinfo(Compiler *c, const DxbcInstruction *ins)
{
    const DxbcOperand *resource = &ins->operands[2];
    Resource *r;
    int slot = resource_slot(c, resource, &r);
    if (slot < 0)
        return;
    spv_capability(&c->b, SpvCapabilityImageQuery);
    uint32_t image_type, image;
    int storage = resource->type == OPERAND_UNORDERED_ACCESS_VIEW;
    if (storage) {
        uint32_t ptr = uav_pointer(c, (uint32_t)slot, &image_type);
        image = spv_load(&c->b, image_type, ptr);
    } else {
        image = load_srv(c, (uint32_t)slot, 0, &image_type);
    }
    uint32_t dims;
    switch (r->dim) {
    case RES_TEXTURE1D:
        dims = 1;
        break;
    case RES_TEXTURE3D:
    case RES_TEXTURE2DARRAY:
    case RES_TEXTURE2DMSARRAY:
    case RES_TEXTURECUBEARRAY:
        dims = 3;
        break;
    default:
        dims = 2;
        break;
    }
    int ms = r->dim == RES_TEXTURE2DMS || r->dim == RES_TEXTURE2DMSARRAY;
    uint32_t size, levels;
    if (storage || ms) {
        size = spv_op1(&c->b, SpvOpImageQuerySize, vec_type(c, K_UINT, dims), image);
        levels = uconst(c, 1);
    } else {
        uint32_t lod = load_scalar(c, &ins->operands[1], ins->operands[1].swizzle[0], K_UINT);
        size = spv_op2(&c->b, SpvOpImageQuerySizeLod, vec_type(c, K_UINT, dims), image, lod);
        levels = spv_op1(&c->b, SpvOpImageQueryLevels, c->t_u32, image);
    }
    uint32_t parts[4];
    for (uint32_t i = 0; i < 3; i++)
        parts[i] = i < dims ? (dims == 1 ? size : extract(c, c->t_u32, size, i)) : uconst(c, 0);
    parts[3] = levels;
    uint32_t value = spv_op(&c->b, SpvOpCompositeConstruct, c->t_vec[K_UINT][4], parts, 4);
    uint32_t mode = TOK_RESINFO_RETURN(ins->token);
    if (mode == 2) {
        store_texel(c, ins, resource, value, K_UINT);
        return;
    }
    uint32_t f = spv_op1(&c->b, SpvOpConvertUToF, c->t_vec[K_FLOAT][4], value);
    if (mode == 1) {
        uint32_t one = fconst(c, 1.0f);
        uint32_t ones[4] = {one, one, one, one};
        uint32_t rcp = spv_op2(&c->b, SpvOpFDiv, c->t_vec[K_FLOAT][4],
                               spv_const_composite(&c->b, c->t_vec[K_FLOAT][4], ones, 4), f);
        uint32_t w[6] = {rcp, f, 0, 1, 2, 7};
        f = spv_op(&c->b, SpvOpVectorShuffle, c->t_vec[K_FLOAT][4], w, 6);
    }
    store_texel(c, ins, resource, f, K_FLOAT);
}

/* the element count of buffer views: bytes for raw ones, structures for structured ones. */
static void bufinfo(Compiler *c, const DxbcInstruction *ins)
{
    const DxbcOperand *resource = &ins->operands[1];
    Resource *r;
    int slot = resource_slot(c, resource, &r);
    if (slot < 0)
        return;
    spv_capability(&c->b, SpvCapabilityImageQuery);
    uint32_t image_type, image;
    if (resource->type == OPERAND_UNORDERED_ACCESS_VIEW) {
        uint32_t ptr = uav_pointer(c, (uint32_t)slot, &image_type);
        image = spv_load(&c->b, image_type, ptr);
    } else {
        image = load_srv(c, (uint32_t)slot, 0, &image_type);
    }
    uint32_t size = spv_op1(&c->b, SpvOpImageQuerySize, c->t_u32, image);
    if (r->raw)
        size = spv_op2(&c->b, SpvOpIMul, c->t_u32, size, uconst(c, 4));
    else if (r->structured && r->stride)
        size = spv_op2(&c->b, SpvOpUDiv, c->t_u32, spv_op2(&c->b, SpvOpIMul, c->t_u32, size, uconst(c, 4)),
                       uconst(c, r->stride));
    store(c, ins, &ins->operands[0], splat(c, K_UINT, popcount(dst_mask(&ins->operands[0])), size), K_UINT);
}

static void lod(Compiler *c, const DxbcInstruction *ins)
{
    const DxbcOperand *resource = &ins->operands[2], *sampler = &ins->operands[3];
    Resource *r;
    int slot = resource_slot(c, resource, &r);
    if (slot < 0)
        return;
    spv_capability(&c->b, SpvCapabilityImageQuery);
    uint32_t image_type;
    uint32_t image = load_srv(c, (uint32_t)slot, 0, &image_type);
    uint32_t si = spv_op2(&c->b, SpvOpSampledImage, spv_type_sampled_image(&c->b, image_type), image,
                          load_sampler(c, sampler->index[0].immediate & 15));
    uint32_t coord = load_first(c, &ins->operands[1], spatial_count(r->dim), K_FLOAT);
    uint32_t v2 = spv_op2(&c->b, SpvOpImageQueryLod, c->t_vec[K_FLOAT][2], si, coord);
    uint32_t zero = fconst(c, 0.0f);
    uint32_t parts[4] = {extract(c, c->t_f32, v2, 0), extract(c, c->t_f32, v2, 1), zero, zero};
    store_texel(c, ins, resource, spv_op(&c->b, SpvOpCompositeConstruct, c->t_vec[K_FLOAT][4], parts, 4), K_FLOAT);
}

static void sampleinfo(Compiler *c, const DxbcInstruction *ins)
{
    const DxbcOperand *resource = &ins->operands[1];
    uint32_t count;
    if (resource->type == OPERAND_RASTERIZER) {
        /* the render target's sample count is not known to the shader: one, as without multisampling. */
        count = uconst(c, 1);
    } else {
        Resource *r;
        int slot = resource_slot(c, resource, &r);
        if (slot < 0)
            return;
        spv_capability(&c->b, SpvCapabilityImageQuery);
        uint32_t image_type;
        uint32_t image = load_srv(c, (uint32_t)slot, 0, &image_type);
        count = spv_op1(&c->b, SpvOpImageQuerySamples, c->t_u32, image);
    }
    int as_uint = TOK_RESINFO_RETURN(ins->token) == 2;
    uint32_t zero = uconst(c, 0);
    uint32_t parts[4] = {count, zero, zero, zero};
    uint32_t v = spv_op(&c->b, SpvOpCompositeConstruct, c->t_vec[K_UINT][4], parts, 4);
    if (!as_uint)
        v = spv_op1(&c->b, SpvOpConvertUToF, c->t_vec[K_FLOAT][4], v);
    store_texel(c, ins, resource, v, as_uint ? K_UINT : K_FLOAT);
}

/* ------------------------------------------------------------------ buffers, unordered access and shared memory */

/* the word an address names: bytes for raw access, structure and byte offset for structured access. */
static uint32_t word_index(Compiler *c, const DxbcOperand *address, const DxbcOperand *offset, uint32_t stride)
{
    uint32_t a = load_scalar(c, address, address->swizzle[0], K_UINT);
    if (offset) {
        uint32_t o = load_scalar(c, offset, offset->swizzle[0], K_UINT);
        a = spv_op2(&c->b, SpvOpIAdd, c->t_u32, spv_op2(&c->b, SpvOpIMul, c->t_u32, a, uconst(c, stride)), o);
    }
    return spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, a, uconst(c, 2));
}

/* reads one word of a raw or structured resource: a view or shared memory. */
static uint32_t read_word(Compiler *c, const DxbcOperand *resource, uint32_t word)
{
    if (resource->type == OPERAND_THREAD_GROUP_SHARED_MEMORY) {
        uint32_t reg = resource->index[0].immediate & 255;
        uint32_t ptr = spv_access(&c->b, spv_type_pointer(&c->b, SpvStorageClassWorkgroup, c->t_u32), c->tgsm[reg],
                                  &word, 1);
        return spv_load(&c->b, c->t_u32, ptr);
    }
    uint32_t slot = resource->index[0].immediate;
    uint32_t image_type, image;
    if (resource->type == OPERAND_UNORDERED_ACCESS_VIEW) {
        uint32_t ptr = uav_pointer(c, slot, &image_type);
        image = spv_load(&c->b, image_type, ptr);
        return extract(c, c->t_u32, spv_op2(&c->b, SpvOpImageRead, c->t_vec[K_UINT][4], image, word), 0);
    }
    image = load_srv(c, slot, 0, &image_type);
    return extract(c, c->t_u32, spv_op2(&c->b, SpvOpImageFetch, c->t_vec[K_UINT][4], image, word), 0);
}

static void write_word(Compiler *c, const DxbcOperand *resource, uint32_t word, uint32_t value)
{
    if (resource->type == OPERAND_THREAD_GROUP_SHARED_MEMORY) {
        uint32_t reg = resource->index[0].immediate & 255;
        uint32_t ptr = spv_access(&c->b, spv_type_pointer(&c->b, SpvStorageClassWorkgroup, c->t_u32), c->tgsm[reg],
                                  &word, 1);
        spv_store(&c->b, ptr, value);
        return;
    }
    uint32_t image_type;
    uint32_t ptr = uav_pointer(c, resource->index[0].immediate, &image_type);
    uint32_t image = spv_load(&c->b, image_type, ptr);
    uint32_t parts[4] = {value, value, value, value};
    uint32_t v = spv_op(&c->b, SpvOpCompositeConstruct, c->t_vec[K_UINT][4], parts, 4);
    uint32_t w[3] = {image, word, v};
    spv_op_void(&c->b, SpvOpImageWrite, w, 3);
}

static uint32_t stride_of(Compiler *c, const DxbcOperand *resource)
{
    uint32_t slot = resource->index[0].immediate;
    if (resource->type == OPERAND_THREAD_GROUP_SHARED_MEMORY)
        return c->tgsm_stride[slot & 255];
    if (resource->type == OPERAND_UNORDERED_ACCESS_VIEW)
        return slot < 64 ? c->uav[slot].stride : 0;
    return slot < 128 ? c->srv[slot].stride : 0;
}

/* ld_raw and ld_structured: the words the resource's swizzle names from the address. */
static void load_words(Compiler *c, const DxbcInstruction *ins, int structured)
{
    const DxbcOperand *resource = &ins->operands[structured ? 3 : 2];
    uint32_t base = word_index(c, &ins->operands[1], structured ? &ins->operands[2] : NULL, stride_of(c, resource));
    uint8_t mask = dst_mask(&ins->operands[0]);
    uint32_t parts[4], n = 0;
    for (uint32_t i = 0; i < 4; i++)
        if (mask & (1 << i)) {
            uint32_t word = spv_op2(&c->b, SpvOpIAdd, c->t_u32, base, uconst(c, resource->swizzle[i]));
            parts[n++] = read_word(c, resource, word);
        }
    uint32_t v = n == 1 ? parts[0] : spv_op(&c->b, SpvOpCompositeConstruct, vec_type(c, K_UINT, n), parts, n);
    store(c, ins, &ins->operands[0], v, K_UINT);
}

/* store_raw and store_structured: the value's components to consecutive words. */
static void store_words(Compiler *c, const DxbcInstruction *ins, int structured)
{
    const DxbcOperand *resource = &ins->operands[0];
    uint32_t base = word_index(c, &ins->operands[1], structured ? &ins->operands[2] : NULL, stride_of(c, resource));
    const DxbcOperand *value = &ins->operands[structured ? 3 : 2];
    uint8_t mask = dst_mask(resource);
    for (uint32_t i = 0; i < 4; i++)
        if (mask & (1 << i)) {
            uint32_t word = spv_op2(&c->b, SpvOpIAdd, c->t_u32, base, uconst(c, i));
            write_word(c, resource, word, load_scalar(c, value, value->swizzle[i], K_UINT));
        }
}

static void load_typed(Compiler *c, const DxbcInstruction *ins)
{
    const DxbcOperand *resource = &ins->operands[2];
    Resource *r;
    int slot = resource_slot(c, resource, &r);
    if (slot < 0)
        return;
    uint32_t image_type;
    uint32_t ptr = uav_pointer(c, (uint32_t)slot, &image_type);
    uint32_t image = spv_load(&c->b, image_type, ptr);
    uint32_t coord = load_first(c, &ins->operands[1], coord_count(r->dim), K_UINT);
    uint32_t v = spv_op2(&c->b, SpvOpImageRead, vec_type(c, r->kind, 4), image, coord);
    store_texel(c, ins, resource, v, r->kind);
}

static void store_typed(Compiler *c, const DxbcInstruction *ins)
{
    const DxbcOperand *resource = &ins->operands[0];
    Resource *r;
    int slot = resource_slot(c, resource, &r);
    if (slot < 0)
        return;
    uint32_t image_type;
    uint32_t ptr = uav_pointer(c, (uint32_t)slot, &image_type);
    uint32_t image = spv_load(&c->b, image_type, ptr);
    uint32_t coord = load_first(c, &ins->operands[1], coord_count(r->dim), K_UINT);
    uint32_t value = load(c, &ins->operands[2], 0xF, r->kind);
    uint32_t w[3] = {image, coord, value};
    spv_op_void(&c->b, SpvOpImageWrite, w, 3);
}

/* a pointer to the word an atomic works on, in a view or in shared memory. */
static uint32_t atomic_pointer(Compiler *c, const DxbcOperand *resource, const DxbcOperand *address, Kind *k,
                               uint32_t *scope)
{
    *k = K_UINT;
    if (resource->type == OPERAND_THREAD_GROUP_SHARED_MEMORY) {
        uint32_t reg = resource->index[0].immediate & 255;
        uint32_t word;
        if (c->tgsm_stride[reg]) {
            DxbcOperand offset = *address;
            offset.swizzle[0] = address->swizzle[1];
            word = word_index(c, address, &offset, c->tgsm_stride[reg]);
        } else {
            word = word_index(c, address, NULL, 0);
        }
        *scope = SpvScopeWorkgroup;
        return spv_access(&c->b, spv_type_pointer(&c->b, SpvStorageClassWorkgroup, c->t_u32), c->tgsm[reg], &word, 1);
    }
    Resource *r;
    int slot = resource_slot(c, resource, &r);
    if (slot < 0)
        return 0;
    r->atomics = 1;
    uint32_t image_type;
    uint32_t image_ptr = uav_pointer(c, (uint32_t)slot, &image_type);
    uint32_t coord;
    if (r->raw) {
        coord = word_index(c, address, NULL, 0);
    } else if (r->structured) {
        DxbcOperand offset = *address;
        offset.swizzle[0] = address->swizzle[1];
        coord = word_index(c, address, &offset, r->stride);
    } else {
        coord = load_first(c, address, coord_count(r->dim), K_UINT);
        *k = r->kind == K_INT ? K_INT : K_UINT;
    }
    *scope = SpvScopeDevice;
    uint32_t w[3] = {image_ptr, coord, uconst(c, 0)};
    return spv_op(&c->b, SpvOpImageTexelPointer, spv_type_pointer(&c->b, SpvStorageClassImage, scalar_type(c, *k)), w, 3);
}

static uint32_t atomic_opcode(uint32_t opcode)
{
    switch (opcode) {
    case OP_ATOMIC_AND:
    case OP_IMM_ATOMIC_AND:
        return SpvOpAtomicAnd;
    case OP_ATOMIC_OR:
    case OP_IMM_ATOMIC_OR:
        return SpvOpAtomicOr;
    case OP_ATOMIC_XOR:
    case OP_IMM_ATOMIC_XOR:
        return SpvOpAtomicXor;
    case OP_ATOMIC_IADD:
    case OP_IMM_ATOMIC_IADD:
        return SpvOpAtomicIAdd;
    case OP_ATOMIC_IMAX:
    case OP_IMM_ATOMIC_IMAX:
        return SpvOpAtomicSMax;
    case OP_ATOMIC_IMIN:
    case OP_IMM_ATOMIC_IMIN:
        return SpvOpAtomicSMin;
    case OP_ATOMIC_UMAX:
    case OP_IMM_ATOMIC_UMAX:
        return SpvOpAtomicUMax;
    case OP_ATOMIC_UMIN:
    case OP_IMM_ATOMIC_UMIN:
        return SpvOpAtomicUMin;
    case OP_IMM_ATOMIC_EXCH:
        return SpvOpAtomicExchange;
    default:
        return SpvOpAtomicCompareExchange;
    }
}

static void atomic(Compiler *c, const DxbcInstruction *ins)
{
    int immediate = ins->opcode >= OP_IMM_ATOMIC_ALLOC;
    const DxbcOperand *resource = &ins->operands[immediate ? 1 : 0];
    const DxbcOperand *address = &ins->operands[immediate ? 2 : 1];
    Kind k;
    uint32_t scope;
    uint32_t ptr = atomic_pointer(c, resource, address, &k, &scope);
    if (!ptr)
        return;
    uint32_t type = scalar_type(c, k);
    uint32_t op = atomic_opcode(ins->opcode);
    uint32_t result;
    if (op == SpvOpAtomicCompareExchange) {
        const DxbcOperand *cmp = &ins->operands[immediate ? 3 : 2], *value = &ins->operands[immediate ? 4 : 3];
        uint32_t w[6] = {ptr, uconst(c, scope), uconst(c, 0), uconst(c, 0),
                         load_scalar(c, value, value->swizzle[0], k), load_scalar(c, cmp, cmp->swizzle[0], k)};
        result = spv_op(&c->b, op, type, w, 6);
    } else {
        const DxbcOperand *value = &ins->operands[immediate ? 3 : 2];
        uint32_t w[4] = {ptr, uconst(c, scope), uconst(c, 0), load_scalar(c, value, value->swizzle[0], k)};
        result = spv_op(&c->b, op, type, w, 4);
    }
    if (immediate)
        store(c, ins, &ins->operands[0], splat(c, k, popcount(dst_mask(&ins->operands[0])), result), k);
}

/* imm_atomic_alloc and consume: the view's hidden counter. */
static void counter(Compiler *c, const DxbcInstruction *ins)
{
    uint32_t slot = ins->operands[1].index[0].immediate & 63;
    uint32_t image_type = spv_type_image(&c->b, c->t_u32, SpvDimBuffer, 0, 0, 0, 2, SpvImageFormatR32ui);
    spv_capability(&c->b, SpvCapabilityImageBuffer);
    uint32_t image_ptr = descriptor_pointer(c, image_type, DXBC_HEAP_STORAGE_TEXEL, DXBC_TABLE_UAV_COUNTER + slot);
    uint32_t w[3] = {image_ptr, uconst(c, 0), uconst(c, 0)};
    uint32_t ptr =
        spv_op(&c->b, SpvOpImageTexelPointer, spv_type_pointer(&c->b, SpvStorageClassImage, c->t_u32), w, 3);
    uint32_t a[3] = {ptr, uconst(c, SpvScopeDevice), uconst(c, 0)};
    uint32_t result;
    if (ins->opcode == OP_IMM_ATOMIC_ALLOC) {
        result = spv_op(&c->b, SpvOpAtomicIIncrement, c->t_u32, a, 3);
    } else {
        result = spv_op(&c->b, SpvOpAtomicIDecrement, c->t_u32, a, 3);
        result = spv_op2(&c->b, SpvOpISub, c->t_u32, result, uconst(c, 1));
    }
    store(c, ins, &ins->operands[0], splat(c, K_UINT, popcount(dst_mask(&ins->operands[0])), result), K_UINT);
}

static void sync(Compiler *c, const DxbcInstruction *ins)
{
    uint32_t flags = TOK_SYNC_FLAGS(ins->token);
    uint32_t memory = (flags & SYNC_UAV_GLOBAL) ? SpvScopeDevice : SpvScopeWorkgroup;
    uint32_t semantics = SpvMemorySemanticsAcquireReleaseMask;
    if (flags & SYNC_GROUP_SHARED_MEMORY)
        semantics |= SpvMemorySemanticsWorkgroupMemoryMask;
    if (flags & (SYNC_UAV_GROUP | SYNC_UAV_GLOBAL))
        semantics |= SpvMemorySemanticsImageMemoryMask | SpvMemorySemanticsUniformMemoryMask;
    if (flags & SYNC_THREADS_IN_GROUP) {
        uint32_t w[3] = {uconst(c, SpvScopeWorkgroup), uconst(c, memory), uconst(c, semantics)};
        spv_op_void(&c->b, SpvOpControlBarrier, w, 3);
    } else {
        uint32_t w[2] = {uconst(c, memory), uconst(c, semantics)};
        spv_op_void(&c->b, SpvOpMemoryBarrier, w, 2);
    }
}

/* eval_*: an input interpolated elsewhere than the pixel's center. */
static void eval(Compiler *c, const DxbcInstruction *ins)
{
    const DxbcOperand *input = &ins->operands[1];
    uint32_t reg = input->index[0].immediate;
    if (input->type != OPERAND_INPUT || reg >= MAX_REGS || !c->input_decl[reg].var) {
        fail(c, "evaluated input without its variable");
        return;
    }
    spv_capability(&c->b, SpvCapabilityInterpolationFunction);
    uint32_t v4 = c->t_vec[K_FLOAT][4];
    uint32_t args[2] = {c->input_decl[reg].var, 0};
    uint32_t instruction = GLSLstd450InterpolateAtCentroid, count = 1;
    if (ins->opcode == OP_EVAL_SAMPLE_INDEX) {
        instruction = GLSLstd450InterpolateAtSample;
        args[1] = load_scalar(c, &ins->operands[2], ins->operands[2].swizzle[0], K_INT);
        count = 2;
    } else if (ins->opcode == OP_EVAL_SNAPPED) {
        /* offsets in sixteenths of a pixel. */
        instruction = GLSLstd450InterpolateAtOffset;
        uint32_t o = load_first(c, &ins->operands[2], 2, K_INT);
        o = spv_op1(&c->b, SpvOpConvertSToF, c->t_vec[K_FLOAT][2], o);
        uint32_t sixteenth = fconst(c, 1.0f / 16.0f);
        uint32_t s2[2] = {sixteenth, sixteenth};
        args[1] = spv_op2(&c->b, SpvOpFMul, c->t_vec[K_FLOAT][2], o,
                          spv_const_composite(&c->b, c->t_vec[K_FLOAT][2], s2, 2));
        count = 2;
    }
    uint32_t v = spv_glsl(&c->b, v4, instruction, args, count);
    const DxbcOperand *d = &ins->operands[0];
    store(c, ins, d, pick(c, v, K_FLOAT, dst_mask(d), input->swizzle), K_FLOAT);
}

static void derivative(Compiler *c, const DxbcInstruction *ins, uint32_t op)
{
    if (op != SpvOpDPdx && op != SpvOpDPdy)
        spv_capability(&c->b, SpvCapabilityDerivativeControl);
    uint8_t mask = dst_mask(&ins->operands[0]);
    uint32_t a = load(c, &ins->operands[1], mask, K_FLOAT);
    store(c, ins, &ins->operands[0], spv_op1(&c->b, op, vec_type(c, K_FLOAT, popcount(mask)), a), K_FLOAT);
}

/* ------------------------------------------------------------------ control flow */

static Flow *push_flow(Compiler *c, int kind)
{
    if (c->flow_depth >= 64) {
        fail(c, "control flow nested too deep");
        return NULL;
    }
    Flow *f = &c->flow[c->flow_depth++];
    __builtin_memset(f, 0, sizeof(*f));
    f->kind = kind;
    return f;
}

/* the innermost loop, or loop or switch for break. */
static Flow *break_target(Compiler *c, int loops_only)
{
    for (uint32_t i = c->flow_depth; i > 0; i--) {
        Flow *f = &c->flow[i - 1];
        if (f->kind == FLOW_LOOP || (!loops_only && f->kind == FLOW_SWITCH))
            return f;
    }
    fail(c, "break or continue outside a loop");
    return NULL;
}

/* the condition of a conditional instruction: its operand compared with zero as the token says. */
static uint32_t condition(Compiler *c, const DxbcInstruction *ins, const DxbcOperand *op)
{
    uint32_t v = load_scalar(c, op, op->swizzle[0], K_UINT);
    return spv_op2(&c->b, ins->nonzero ? SpvOpINotEqual : SpvOpIEqual, c->t_bool, v, uconst(c, 0));
}

/* opens a selection that runs a block when the condition holds, returning the block's label. */
static uint32_t begin_conditional(Compiler *c, uint32_t cond, uint32_t *merge)
{
    uint32_t then = spv_id(&c->b);
    *merge = spv_id(&c->b);
    uint32_t w[2] = {*merge, SpvSelectionControlMaskNone};
    spv_op_void(&c->b, SpvOpSelectionMerge, w, 2);
    spv_branch_conditional(&c->b, cond, then, *merge);
    spv_label(&c->b, then);
    return then;
}

/* code after a branch that left the block: unreachable, in a block of its own. */
static void dead_block(Compiler *c) { spv_label(&c->b, spv_id(&c->b)); }

/* the case values and labels of the switch starting at the reader's position, its nested switches skipped. */
static int scan_switch(Compiler *c, uint32_t at, Flow *f)
{
    DxbcReader r = {c->code, c->code_count, at};
    DxbcInstruction ins;
    uint32_t depth = 0, capacity = 16;
    f->labels = mime_alloc(c->alloc, capacity * sizeof(uint32_t));
    f->values = mime_alloc(c->alloc, capacity * sizeof(uint32_t));
    if (!f->labels || !f->values)
        return fail(c, "out of memory");
    uint32_t label = 0;
    int previous_was_case = 0;
    while (dxbc_read(&r, &ins) > 0) {
        if (ins.opcode == OP_SWITCH) {
            depth++;
        } else if (ins.opcode == OP_ENDSWITCH) {
            if (!depth--)
                return 0;
        } else if (!depth && (ins.opcode == OP_CASE || ins.opcode == OP_DEFAULT)) {
            /* labels in a row share their block. */
            if (!previous_was_case)
                label = spv_id(&c->b);
            if (ins.opcode == OP_DEFAULT) {
                f->default_label = label;
            } else {
                if (f->count == capacity) {
                    uint32_t *labels = mime_alloc(c->alloc, capacity * 2 * sizeof(uint32_t));
                    uint32_t *values = mime_alloc(c->alloc, capacity * 2 * sizeof(uint32_t));
                    if (!labels || !values)
                        return fail(c, "out of memory");
                    __builtin_memcpy(labels, f->labels, capacity * 4);
                    __builtin_memcpy(values, f->values, capacity * 4);
                    mime_free(c->alloc, f->labels);
                    mime_free(c->alloc, f->values);
                    f->labels = labels, f->values = values;
                    capacity *= 2;
                }
                f->labels[f->count] = label;
                f->values[f->count] = ins.operands[0].immediate[0];
                f->count++;
            }
            previous_was_case = 1;
            continue;
        }
        previous_was_case = 0;
    }
    return fail(c, "switch without endswitch");
}

static void close_flow(Compiler *c, Flow *f)
{
    mime_free(c->alloc, f->labels);
    mime_free(c->alloc, f->values);
    c->flow_depth--;
}

static int control_flow(Compiler *c, const DxbcInstruction *ins, uint32_t at)
{
    SpvBuilder *b = &c->b;
    switch (ins->opcode) {
    case OP_IF: {
        Flow *f = push_flow(c, FLOW_IF);
        if (!f)
            return -1;
        uint32_t cond = condition(c, ins, &ins->operands[0]);
        uint32_t then = spv_id(b);
        f->merge = spv_id(b);
        f->else_label = spv_id(b);
        uint32_t w[2] = {f->merge, SpvSelectionControlMaskNone};
        spv_op_void(b, SpvOpSelectionMerge, w, 2);
        spv_branch_conditional(b, cond, then, f->else_label);
        spv_label(b, then);
        return 0;
    }
    case OP_ELSE: {
        Flow *f = c->flow_depth ? &c->flow[c->flow_depth - 1] : NULL;
        if (!f || f->kind != FLOW_IF || !f->else_label)
            return fail(c, "else without if");
        if (b->block_open)
            spv_branch(b, f->merge);
        spv_label(b, f->else_label);
        f->else_label = 0;
        return 0;
    }
    case OP_ENDIF: {
        Flow *f = c->flow_depth ? &c->flow[c->flow_depth - 1] : NULL;
        if (!f || f->kind != FLOW_IF)
            return fail(c, "endif without if");
        if (b->block_open)
            spv_branch(b, f->merge);
        if (f->else_label) {
            spv_label(b, f->else_label);
            spv_branch(b, f->merge);
        }
        spv_label(b, f->merge);
        close_flow(c, f);
        return 0;
    }
    case OP_LOOP: {
        Flow *f = push_flow(c, FLOW_LOOP);
        if (!f)
            return -1;
        f->header = spv_id(b);
        f->merge = spv_id(b);
        f->cont = spv_id(b);
        uint32_t body = spv_id(b);
        spv_label(b, f->header);
        uint32_t w[3] = {f->merge, f->cont, SpvLoopControlMaskNone};
        spv_op_void(b, SpvOpLoopMerge, w, 3);
        spv_branch(b, body);
        spv_label(b, body);
        return 0;
    }
    case OP_ENDLOOP: {
        Flow *f = c->flow_depth ? &c->flow[c->flow_depth - 1] : NULL;
        if (!f || f->kind != FLOW_LOOP)
            return fail(c, "endloop without loop");
        if (b->block_open)
            spv_branch(b, f->cont);
        spv_label(b, f->cont);
        spv_branch(b, f->header);
        spv_label(b, f->merge);
        close_flow(c, f);
        return 0;
    }
    case OP_BREAK:
    case OP_CONTINUE: {
        Flow *f = break_target(c, ins->opcode == OP_CONTINUE);
        if (!f)
            return -1;
        spv_branch(b, ins->opcode == OP_BREAK ? f->merge : f->cont);
        dead_block(c);
        return 0;
    }
    case OP_BREAKC:
    case OP_CONTINUEC: {
        Flow *f = break_target(c, ins->opcode == OP_CONTINUEC);
        if (!f)
            return -1;
        uint32_t merge;
        begin_conditional(c, condition(c, ins, &ins->operands[0]), &merge);
        spv_branch(b, ins->opcode == OP_BREAKC ? f->merge : f->cont);
        spv_label(b, merge);
        return 0;
    }
    case OP_SWITCH: {
        Flow *f = push_flow(c, FLOW_SWITCH);
        if (!f)
            return -1;
        if (scan_switch(c, at + ins->length, f))
            return -1;
        f->merge = spv_id(b);
        if (!f->default_label)
            f->default_label = f->merge;
        uint32_t selector = load_scalar(c, &ins->operands[0], ins->operands[0].swizzle[0], K_UINT);
        uint32_t w[2] = {f->merge, SpvSelectionControlMaskNone};
        spv_op_void(b, SpvOpSelectionMerge, w, 2);
        uint32_t *words = mime_alloc(c->alloc, (2 + 2 * f->count) * sizeof(uint32_t));
        if (!words)
            return fail(c, "out of memory");
        words[0] = selector;
        words[1] = f->default_label;
        for (uint32_t i = 0; i < f->count; i++) {
            words[2 + 2 * i] = f->values[i];
            words[3 + 2 * i] = f->labels[i];
        }
        spv_op_void(b, SpvOpSwitch, words, 2 + 2 * f->count);
        b->block_open = 0;
        mime_free(c->alloc, words);
        return 0;
    }
    case OP_CASE:
    case OP_DEFAULT: {
        Flow *f = c->flow_depth ? &c->flow[c->flow_depth - 1] : NULL;
        if (!f || f->kind != FLOW_SWITCH)
            return fail(c, "case outside a switch");
        uint32_t label;
        if (ins->opcode == OP_DEFAULT)
            label = f->default_label;
        else {
            if (f->next >= f->count)
                return fail(c, "switch cases out of order");
            label = f->labels[f->next++];
        }
        /* a label shared with the case before it is already open. */
        if (b->block_open && f->open_label == label)
            return 0;
        spv_label(b, label);
        f->open_label = label;
        return 0;
    }
    case OP_ENDSWITCH: {
        Flow *f = c->flow_depth ? &c->flow[c->flow_depth - 1] : NULL;
        if (!f || f->kind != FLOW_SWITCH)
            return fail(c, "endswitch without switch");
        if (b->block_open)
            spv_branch(b, f->merge);
        spv_label(b, f->merge);
        close_flow(c, f);
        return 0;
    }
    case OP_RET:
        spv_return(b);
        dead_block(c);
        return 0;
    case OP_RETC: {
        uint32_t merge;
        begin_conditional(c, condition(c, ins, &ins->operands[0]), &merge);
        spv_return(b);
        spv_label(b, merge);
        return 0;
    }
    case OP_DISCARD: {
        uint32_t merge;
        begin_conditional(c, condition(c, ins, &ins->operands[0]), &merge);
        spv_capability(b, SpvCapabilityDemoteToHelperInvocation);
        spv_op_void(b, SpvOpDemoteToHelperInvocation, NULL, 0);
        spv_branch(b, merge);
        spv_label(b, merge);
        c->info->uses_discard = 1;
        return 0;
    }
    case OP_CALL:
    case OP_CALLC: {
        uint32_t number = ins->operands[ins->opcode == OP_CALLC ? 1 : 0].index[0].immediate;
        uint32_t fn = 0;
        for (uint32_t i = 0; i < c->label_count; i++)
            if (c->label_number[i] == number)
                fn = c->label_fn[i];
        if (!fn)
            return fail(c, "call to an unknown label");
        uint32_t merge = 0;
        if (ins->opcode == OP_CALLC)
            begin_conditional(c, condition(c, ins, &ins->operands[0]), &merge);
        spv_op(b, SpvOpFunctionCall, c->t_void, &fn, 1);
        if (merge)
            spv_label(b, merge);
        return 0;
    }
    default:
        return fail(c, "unknown control flow");
    }
}

/* ------------------------------------------------------------------ instructions */

static int instruction(Compiler *c, const DxbcInstruction *ins, uint32_t at)
{
    switch (ins->opcode) {
    case OP_ADD:
        alu2(c, ins, SpvOpFAdd, K_FLOAT);
        break;
    case OP_MUL:
        alu2(c, ins, SpvOpFMul, K_FLOAT);
        break;
    case OP_DIV:
        alu2(c, ins, SpvOpFDiv, K_FLOAT);
        break;
    case OP_MAD:
        mad(c, ins, K_FLOAT);
        break;
    case OP_MIN:
        glsl2(c, ins, GLSLstd450NMin, K_FLOAT);
        break;
    case OP_MAX:
        glsl2(c, ins, GLSLstd450NMax, K_FLOAT);
        break;
    case OP_MOV:
    case OP_DMOV: {
        uint8_t mask = dst_mask(&ins->operands[0]);
        /* moves keep bits as they are, but saturating or modifying them is done on floats. */
        Kind k = ins->saturate || ins->operands[1].modifier ? K_FLOAT : K_UINT;
        store(c, ins, &ins->operands[0], load(c, &ins->operands[1], mask, k), k);
        break;
    }
    case OP_MOVC:
        movc(c, ins);
        break;
    case OP_SWAPC:
        swapc(c, ins);
        break;
    case OP_DP2:
        dot(c, ins, 2);
        break;
    case OP_DP3:
        dot(c, ins, 3);
        break;
    case OP_DP4:
        dot(c, ins, 4);
        break;
    case OP_EQ:
        compare(c, ins, SpvOpFOrdEqual, K_FLOAT);
        break;
    case OP_NE:
        compare(c, ins, SpvOpFUnordNotEqual, K_FLOAT);
        break;
    case OP_LT:
        compare(c, ins, SpvOpFOrdLessThan, K_FLOAT);
        break;
    case OP_GE:
        compare(c, ins, SpvOpFOrdGreaterThanEqual, K_FLOAT);
        break;
    case OP_IEQ:
        compare(c, ins, SpvOpIEqual, K_UINT);
        break;
    case OP_INE:
        compare(c, ins, SpvOpINotEqual, K_UINT);
        break;
    case OP_ILT:
        compare(c, ins, SpvOpSLessThan, K_INT);
        break;
    case OP_IGE:
        compare(c, ins, SpvOpSGreaterThanEqual, K_INT);
        break;
    case OP_ULT:
        compare(c, ins, SpvOpULessThan, K_UINT);
        break;
    case OP_UGE:
        compare(c, ins, SpvOpUGreaterThanEqual, K_UINT);
        break;
    case OP_EXP:
        glsl1(c, ins, GLSLstd450Exp2, K_FLOAT);
        break;
    case OP_LOG:
        glsl1(c, ins, GLSLstd450Log2, K_FLOAT);
        break;
    case OP_SQRT:
        glsl1(c, ins, GLSLstd450Sqrt, K_FLOAT);
        break;
    case OP_RSQ:
        glsl1(c, ins, GLSLstd450InverseSqrt, K_FLOAT);
        break;
    case OP_RCP:
        rcp(c, ins);
        break;
    case OP_FRC:
        glsl1(c, ins, GLSLstd450Fract, K_FLOAT);
        break;
    case OP_ROUND_NE:
        glsl1(c, ins, GLSLstd450RoundEven, K_FLOAT);
        break;
    case OP_ROUND_NI:
        glsl1(c, ins, GLSLstd450Floor, K_FLOAT);
        break;
    case OP_ROUND_PI:
        glsl1(c, ins, GLSLstd450Ceil, K_FLOAT);
        break;
    case OP_ROUND_Z:
        glsl1(c, ins, GLSLstd450Trunc, K_FLOAT);
        break;
    case OP_SINCOS:
        sin_cos(c, ins);
        break;
    case OP_FTOI:
        alu1(c, ins, SpvOpConvertFToS, K_FLOAT, K_INT);
        break;
    case OP_FTOU:
        alu1(c, ins, SpvOpConvertFToU, K_FLOAT, K_UINT);
        break;
    case OP_ITOF:
        alu1(c, ins, SpvOpConvertSToF, K_INT, K_FLOAT);
        break;
    case OP_UTOF:
        alu1(c, ins, SpvOpConvertUToF, K_UINT, K_FLOAT);
        break;
    case OP_IADD:
        alu2(c, ins, SpvOpIAdd, K_UINT);
        break;
    case OP_IMAD:
        mad(c, ins, K_INT);
        break;
    case OP_UMAD:
        mad(c, ins, K_UINT);
        break;
    case OP_IMAX:
        glsl2(c, ins, GLSLstd450SMax, K_INT);
        break;
    case OP_IMIN:
        glsl2(c, ins, GLSLstd450SMin, K_INT);
        break;
    case OP_UMAX:
        glsl2(c, ins, GLSLstd450UMax, K_UINT);
        break;
    case OP_UMIN:
        glsl2(c, ins, GLSLstd450UMin, K_UINT);
        break;
    case OP_INEG:
        alu1(c, ins, SpvOpSNegate, K_INT, K_INT);
        break;
    case OP_NOT:
        alu1(c, ins, SpvOpNot, K_UINT, K_UINT);
        break;
    case OP_AND:
        alu2(c, ins, SpvOpBitwiseAnd, K_UINT);
        break;
    case OP_OR:
        alu2(c, ins, SpvOpBitwiseOr, K_UINT);
        break;
    case OP_XOR:
        alu2(c, ins, SpvOpBitwiseXor, K_UINT);
        break;
    case OP_ISHL:
        shift(c, ins, SpvOpShiftLeftLogical, K_UINT);
        break;
    case OP_ISHR:
        shift(c, ins, SpvOpShiftRightArithmetic, K_INT);
        break;
    case OP_USHR:
        shift(c, ins, SpvOpShiftRightLogical, K_UINT);
        break;
    case OP_IMUL:
        wide(c, ins, SpvOpSMulExtended, K_INT, 1);
        break;
    case OP_UMUL:
        wide(c, ins, SpvOpUMulExtended, K_UINT, 1);
        break;
    case OP_UADDC:
        wide(c, ins, SpvOpIAddCarry, K_UINT, 0);
        break;
    case OP_USUBB:
        wide(c, ins, SpvOpISubBorrow, K_UINT, 0);
        break;
    case OP_UDIV:
        udiv(c, ins);
        break;
    case OP_COUNTBITS:
        alu1(c, ins, SpvOpBitCount, K_UINT, K_UINT);
        break;
    case OP_BFREV:
        alu1(c, ins, SpvOpBitReverse, K_UINT, K_UINT);
        break;
    case OP_FIRSTBIT_HI:
        firstbit(c, ins, GLSLstd450FindUMsb);
        break;
    case OP_FIRSTBIT_SHI:
        firstbit(c, ins, GLSLstd450FindSMsb);
        break;
    case OP_FIRSTBIT_LO:
        firstbit(c, ins, GLSLstd450FindILsb);
        break;
    case OP_UBFE:
        bfe(c, ins, K_UINT);
        break;
    case OP_IBFE:
        bfe(c, ins, K_INT);
        break;
    case OP_BFI:
        bfi(c, ins);
        break;
    case OP_F32TOF16:
        half(c, ins, 1);
        break;
    case OP_F16TOF32:
        half(c, ins, 0);
        break;
    case OP_DERIV_RTX:
    case OP_DERIV_RTX_COARSE:
        derivative(c, ins, ins->opcode == OP_DERIV_RTX ? SpvOpDPdx : SpvOpDPdxCoarse);
        break;
    case OP_DERIV_RTY:
    case OP_DERIV_RTY_COARSE:
        derivative(c, ins, ins->opcode == OP_DERIV_RTY ? SpvOpDPdy : SpvOpDPdyCoarse);
        break;
    case OP_DERIV_RTX_FINE:
        derivative(c, ins, SpvOpDPdxFine);
        break;
    case OP_DERIV_RTY_FINE:
        derivative(c, ins, SpvOpDPdyFine);
        break;
    case OP_SAMPLE:
    case OP_SAMPLE_B:
    case OP_SAMPLE_L:
    case OP_SAMPLE_D:
    case OP_SAMPLE_C:
    case OP_SAMPLE_C_LZ:
        sample(c, ins);
        break;
    case OP_GATHER4:
    case OP_GATHER4_C:
    case OP_GATHER4_PO:
    case OP_GATHER4_PO_C:
        gather(c, ins);
        break;
    case OP_LD:
    case OP_LD_MS:
        fetch(c, ins);
        break;
    case OP_RESINFO:
        resinfo(c, ins);
        break;
    case OP_BUFINFO:
        bufinfo(c, ins);
        break;
    case OP_LOD:
        lod(c, ins);
        break;
    case OP_SAMPLE_INFO:
        sampleinfo(c, ins);
        break;
    case OP_LD_RAW:
        load_words(c, ins, 0);
        break;
    case OP_LD_STRUCTURED:
        load_words(c, ins, 1);
        break;
    case OP_STORE_RAW:
        store_words(c, ins, 0);
        break;
    case OP_STORE_STRUCTURED:
        store_words(c, ins, 1);
        break;
    case OP_LD_UAV_TYPED:
        load_typed(c, ins);
        break;
    case OP_STORE_UAV_TYPED:
        store_typed(c, ins);
        break;
    case OP_ATOMIC_AND:
    case OP_ATOMIC_OR:
    case OP_ATOMIC_XOR:
    case OP_ATOMIC_CMP_STORE:
    case OP_ATOMIC_IADD:
    case OP_ATOMIC_IMAX:
    case OP_ATOMIC_IMIN:
    case OP_ATOMIC_UMAX:
    case OP_ATOMIC_UMIN:
    case OP_IMM_ATOMIC_IADD:
    case OP_IMM_ATOMIC_AND:
    case OP_IMM_ATOMIC_OR:
    case OP_IMM_ATOMIC_XOR:
    case OP_IMM_ATOMIC_EXCH:
    case OP_IMM_ATOMIC_CMP_EXCH:
    case OP_IMM_ATOMIC_IMAX:
    case OP_IMM_ATOMIC_IMIN:
    case OP_IMM_ATOMIC_UMAX:
    case OP_IMM_ATOMIC_UMIN:
        atomic(c, ins);
        break;
    case OP_IMM_ATOMIC_ALLOC:
    case OP_IMM_ATOMIC_CONSUME:
        counter(c, ins);
        break;
    case OP_SYNC:
        sync(c, ins);
        break;
    case OP_EVAL_CENTROID:
    case OP_EVAL_SAMPLE_INDEX:
    case OP_EVAL_SNAPPED:
        eval(c, ins);
        break;
    case OP_NOP:
    case OP_CUSTOMDATA:
        break;
    case OP_IF:
    case OP_ELSE:
    case OP_ENDIF:
    case OP_LOOP:
    case OP_ENDLOOP:
    case OP_BREAK:
    case OP_BREAKC:
    case OP_CONTINUE:
    case OP_CONTINUEC:
    case OP_SWITCH:
    case OP_CASE:
    case OP_DEFAULT:
    case OP_ENDSWITCH:
    case OP_RET:
    case OP_RETC:
    case OP_DISCARD:
    case OP_CALL:
    case OP_CALLC:
        return control_flow(c, ins, at);
    default:
        if (ins->opcode >= OP_DCL_RESOURCE && ins->opcode <= OP_DCL_GLOBAL_FLAGS)
            break;
        if (ins->opcode >= OP_DCL_STREAM && ins->opcode <= OP_DCL_RESOURCE_STRUCTURED)
            break;
        if (ins->opcode == OP_DCL_GS_INSTANCE_COUNT)
            break;
        return fail(c, "instruction not implemented");
    }
    return c->error ? -1 : 0;
}

/* the code from at to end, a function body. */
static int compile_range(Compiler *c, uint32_t at, uint32_t end)
{
    DxbcReader r = {c->code, end, at};
    DxbcInstruction ins;
    int res;
    while ((res = dxbc_read(&r, &ins)) > 0) {
        if (ins.opcode == OP_LABEL)
            continue;
        if (instruction(c, &ins, (uint32_t)(ins.words - c->code)))
            return -1;
    }
    if (c->flow_depth)
        return fail(c, "unclosed control flow");
    return res < 0 ? fail(c, "malformed bytecode") : 0;
}

/* ------------------------------------------------------------------ the module */

static void make_types(Compiler *c)
{
    SpvBuilder *b = &c->b;
    c->t_void = spv_type_void(b);
    c->t_bool = spv_type_bool(b);
    c->t_f32 = spv_type_float(b, 32);
    c->t_u32 = spv_type_int(b, 32, 0);
    c->t_i32 = spv_type_int(b, 32, 1);
    uint32_t scalars[K_KINDS] = {c->t_f32, c->t_u32, c->t_i32};
    for (int k = 0; k < K_KINDS; k++)
        for (uint32_t n = 1; n <= 4; n++)
            c->t_vec[k][n] = spv_type_vector(b, scalars[k], n);
    for (uint32_t n = 1; n <= 4; n++)
        c->t_bvec[n] = spv_type_vector(b, c->t_bool, n);
}

static uint32_t table_binding(DxbcProgram program)
{
    static const uint32_t BINDING[] = {[DXBC_VERTEX] = 0, [DXBC_HULL] = 1,  [DXBC_DOMAIN] = 2,
                                       [DXBC_GEOMETRY] = 3, [DXBC_PIXEL] = 4, [DXBC_COMPUTE] = 5};
    return BINDING[program];
}

static int build(Compiler *c)
{
    SpvBuilder *b = &c->b;
    spv_capability(b, SpvCapabilityShader);
    spv_capability(b, SpvCapabilityRuntimeDescriptorArray);
    make_types(c);
    if (prepass(c))
        return -1;
    make_registers(c);

    /* the body, then the subroutines. */
    uint32_t void_fn = spv_type_function(b, c->t_void, NULL, 0);
    c->body = spv_id(b);
    spv_function_begin(b, c->body, c->t_void, void_fn);
    spv_label(b, spv_id(b));
    uint32_t end = c->label_count ? c->label_at[0] : c->code_count;
    if (compile_range(c, 0, end))
        return -1;
    spv_function_end(b);
    for (uint32_t i = 0; i < c->label_count; i++) {
        spv_function_begin(b, c->label_fn[i], c->t_void, void_fn);
        spv_label(b, spv_id(b));
        uint32_t stop = i + 1 < c->label_count ? c->label_at[i + 1] : c->code_count;
        if (compile_range(c, c->label_at[i], stop))
            return -1;
        spv_function_end(b);
    }

    /* the entry point: inputs in, the body, outputs out. */
    uint32_t main = spv_id(b);
    spv_function_begin(b, main, c->t_void, void_fn);
    spv_label(b, spv_id(b));
    if (c->inputs)
        load_inputs(c);
    spv_op(b, SpvOpFunctionCall, c->t_void, &c->body, 1);
    store_outputs(c);
    spv_function_end(b);
    if (c->error)
        return -1;

    static const uint32_t MODELS[] = {[DXBC_PIXEL] = SpvExecutionModelFragment,
                                      [DXBC_VERTEX] = SpvExecutionModelVertex,
                                      [DXBC_GEOMETRY] = SpvExecutionModelGeometry,
                                      [DXBC_HULL] = SpvExecutionModelTessellationControl,
                                      [DXBC_DOMAIN] = SpvExecutionModelTessellationEvaluation,
                                      [DXBC_COMPUTE] = SpvExecutionModelGLCompute};
    uint32_t model[2] = {MODELS[c->program], main};
    spv_emit_string(b, SECTION_ENTRY_POINTS, SpvOpEntryPoint, model, 2, "main", c->interface, c->interface_count);
    if (c->program == DXBC_PIXEL) {
        uint32_t w[2] = {main, SpvExecutionModeOriginUpperLeft};
        spv_emit(b, SECTION_EXECUTION_MODES, SpvOpExecutionMode, w, 2);
        if (c->depth_output) {
            uint32_t mode = c->info->writes_depth == OPERAND_OUTPUT_DEPTH_GREATER_EQUAL ? SpvExecutionModeDepthGreater
                            : c->info->writes_depth == OPERAND_OUTPUT_DEPTH_LESS_EQUAL ? SpvExecutionModeDepthLess
                                                                                       : 0;
            uint32_t d[2] = {main, SpvExecutionModeDepthReplacing};
            spv_emit(b, SECTION_EXECUTION_MODES, SpvOpExecutionMode, d, 2);
            if (mode) {
                d[1] = mode;
                spv_emit(b, SECTION_EXECUTION_MODES, SpvOpExecutionMode, d, 2);
            }
        }
        /* forceEarlyDepthStencil of the global flags. */
        if (c->global_flags & 4) {
            uint32_t e[2] = {main, SpvExecutionModeEarlyFragmentTests};
            spv_emit(b, SECTION_EXECUTION_MODES, SpvOpExecutionMode, e, 2);
        }
    } else if (c->program == DXBC_COMPUTE) {
        uint32_t w[5] = {main, SpvExecutionModeLocalSize, c->thread_group[0] ? c->thread_group[0] : 1,
                         c->thread_group[1] ? c->thread_group[1] : 1, c->thread_group[2] ? c->thread_group[2] : 1};
        spv_emit(b, SECTION_EXECUTION_MODES, SpvOpExecutionMode, w, 5);
    }
    return b->failed ? fail(c, "out of memory") : 0;
}

int dxbc_compile(const void *bytecode, size_t length, const DxbcCompileOptions *options, DxbcShaderInfo *info,
                 const MimeAllocator *alloc)
{
    __builtin_memset(info, 0, sizeof(*info));
    uint32_t size;
    const uint32_t *code = dxbc_find_chunk(bytecode, length, DXBC_FOURCC('S', 'H', 'E', 'X'), &size);
    if (!code)
        code = dxbc_find_chunk(bytecode, length, DXBC_FOURCC('S', 'H', 'D', 'R'), &size);
    if (!code || size < 8 || code[1] * 4 > size || code[1] < 2) {
        __builtin_memcpy(info->error, "no shader code", 15);
        return -1;
    }
    info->program = (DxbcProgram)(code[0] >> 16);
    info->major = (code[0] >> 4) & 0xF;
    info->minor = code[0] & 0xF;
    if (info->program > DXBC_COMPUTE) {
        __builtin_memcpy(info->error, "not a shader program", 21);
        return -1;
    }
    dxbc_parse_io(bytecode, length, 0, &info->inputs, alloc);
    dxbc_parse_io(bytecode, length, 1, &info->outputs, alloc);
    dxbc_parse_signature(bytecode, length, DXBC_FOURCC('P', 'C', 'S', 'G'), &info->patch_constants, alloc);

    Compiler *c = mime_alloc(alloc, sizeof(*c));
    if (!c) {
        __builtin_memcpy(info->error, "out of memory", 14);
        return -1;
    }
    __builtin_memset(c, 0, sizeof(*c));
    c->alloc = alloc;
    c->info = info;
    c->options = options;
    c->program = info->program;
    c->binding = table_binding(info->program);
    c->code = code + 2;
    c->code_count = code[1] - 2;
    spv_init(&c->b, alloc);
    int result = build(c);
    if (!result)
        info->spirv = spv_finish(&c->b, &info->spirv_words);
    if (result || !info->spirv) {
        const char *error = c->error ? c->error : "out of memory";
        size_t n = __builtin_strlen(error);
        if (n >= sizeof(info->error))
            n = sizeof(info->error) - 1;
        __builtin_memcpy(info->error, error, n);
        info->error[n] = 0;
        result = -1;
    }
    for (uint32_t i = 0; i < c->flow_depth; i++) {
        mime_free(alloc, c->flow[i].labels);
        mime_free(alloc, c->flow[i].values);
    }
    spv_free(&c->b);
    mime_free(alloc, c);
    return result;
}

void dxbc_free(DxbcShaderInfo *info, const MimeAllocator *alloc)
{
    mime_free(alloc, info->inputs.elements);
    mime_free(alloc, info->outputs.elements);
    mime_free(alloc, info->patch_constants.elements);
    mime_free(alloc, info->spirv);
    __builtin_memset(info, 0, sizeof(*info));
}
