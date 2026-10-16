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
    /* shader model 5.1: the registers and the space the range covers. */
    uint32_t lower, space;
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
    /* the compact table: the position plus one of each entry the code reads. */
    uint16_t table_map[DXBC_TABLE_DWORDS];

    /* types; those of doubles made at their first use, with their capability. */
    uint32_t t_void, t_bool, t_f32, t_u32, t_i32;
    uint32_t t_f64, t_dvec2;
    /* dcl_hs_maxtessfactor, the bits of a float; 0 without one. */
    uint32_t max_tess_factor;
    /* the clamp of the level of detail of the sample being compiled, for the clamped forms. */
    const DxbcOperand *min_lod;
    /* a form that writes a residency status is being compiled: its read's status, 0 where all was mapped. */
    int feedback;
    uint32_t feedback_status;
    uint32_t sample_positions;
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
        uint32_t image_type, binding;
        uint32_t var;
    } image_vars[64];
    uint32_t image_var_count;
    Resource srv[128];
    Resource uav[64];
    uint32_t sampler_mode[16];
    /* shader model 5.1: the first register and the space of each constant buffer and sampler range. */
    uint32_t cb_lower[32], cb_space[32];
    uint32_t sampler_lower[16], sampler_space[16];
    int sm51;
    /* direct3d 12: the push constants, the static samplers and the records of the heap. */
    uint32_t push;
    uint32_t static_samplers;
    uint32_t records;
    uint32_t root_vec4_pointer, root_word_pointer;

    /* built-in variables, made when first used: inputs and outputs apart. */
    uint32_t builtins[2][64];

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

    /* geometry shaders: vertices per input primitive, the output topology and vertex count, instances, streams. */
    uint32_t gs_primitive;
    uint32_t gs_vertices;
    uint32_t gs_topology;
    uint32_t gs_max_vertices;
    uint32_t gs_instances;
    uint32_t gs_streams;
    /* stream output: an output variable for each captured declaration entry, and the stream rasterized. */
    struct Capture {
        uint32_t var, reg, stream;
        uint8_t first, count;
        Kind kind;
    } *captures;
    uint32_t capture_count;
    int captures_declared;
    uint32_t raster_stream;

    /*
     * hull and domain shaders: the phase being read, the control point
     * counts, the tessellator's description, and the phases of a hull
     * shader with where their code is.
     */
    int phase;
    uint32_t cp_in, cp_out;
    uint32_t tess_domain, tess_partitioning, tess_primitive;
    int has_cp_phase;
    uint32_t cp_start, cp_end;
    struct {
        uint32_t start, end, instances, fn;
    } phases[32];
    uint32_t phase_count;
    InputDecl patch_decl[MAX_REGS];
    uint32_t patch_count;
    uint32_t patch_regs;
    uint32_t cp_outputs[MAX_REGS];
    uint32_t phase_instance;
} Compiler;

enum { PHASE_NONE, PHASE_DECLS, PHASE_CONTROL_POINT, PHASE_FORK, PHASE_JOIN };

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

static uint32_t fconst_bits(Compiler *c, uint32_t bits) { return spv_const(&c->b, c->t_f32, bits); }

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

/* the table entry, a heap index, at its place in the shader's compact table. */
static uint32_t table_entry(Compiler *c, uint32_t entry)
{
    if (!c->table_map[entry]) {
        c->info->table_entries[c->info->table_count] = (uint16_t)entry;
        c->table_map[entry] = (uint16_t)++c->info->table_count;
    }
    uint32_t at = c->table_map[entry] - 1u;
    uint32_t indices[3] = {uconst(c, 0), uconst(c, at / 4), uconst(c, at % 4)};
    uint32_t ptr = spv_access(&c->b, spv_type_pointer(&c->b, SpvStorageClassUniform, c->t_u32), c->table, indices, 3);
    return spv_load(&c->b, c->t_u32, ptr);
}

/* the heap's array of an image type: a variable aliasing the binding of its descriptor kind. */
static uint32_t image_heap(Compiler *c, uint32_t image_type, uint32_t binding)
{
    for (uint32_t i = 0; i < c->image_var_count; i++)
        if (c->image_vars[i].image_type == image_type && c->image_vars[i].binding == binding)
            return c->image_vars[i].var;
    uint32_t array = spv_type_runtime_array(&c->b, image_type);
    uint32_t var = global(c, SpvStorageClassUniformConstant, array, 0);
    /* direct3d 12's descriptors are all of one mutable binding. */
    int root = c->options && c->options->root_signature;
    decorate1(c, var, SpvDecorationDescriptorSet, root ? DXBC_ROOT_SET_RESOURCES : DXBC_SET_HEAP);
    decorate1(c, var, SpvDecorationBinding, root ? 0 : binding);
    if (c->image_var_count < 64) {
        c->image_vars[c->image_var_count].image_type = image_type;
        c->image_vars[c->image_var_count].binding = binding;
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

static uint32_t index_value(Compiler *c, const DxbcIndex *index);

/* ------------------------------------------------------------------ the root signature */

/* the binding of the root signature that covers a register of a space. */
static const DxbcRootBinding *root_binding(Compiler *c, uint32_t type, uint32_t space, uint32_t reg)
{
    const DxbcCompileOptions *o = c->options;
    for (uint32_t i = 0; i < o->root_count; i++) {
        const DxbcRootBinding *b = &o->root[i];
        if (b->type == type && b->space == space && reg >= b->base && (b->count == ~0u || reg - b->base < b->count))
            return b;
    }
    fail(c, "register outside the root signature");
    return NULL;
}

/* a dword of the push constants, the offset a uint value in bytes. */
static uint32_t push_word(Compiler *c, uint32_t byte_offset)
{
    if (!c->push) {
        uint32_t array = spv_type_array_unique(&c->b, c->t_u32, 64);
        decorate1(c, array, SpvDecorationArrayStride, 4);
        uint32_t block = spv_type_struct_unique(&c->b, &array, 1);
        uint32_t offset = 0;
        spv_member_decorate(&c->b, block, 0, SpvDecorationOffset, &offset, 1);
        spv_decorate(&c->b, block, SpvDecorationBlock, NULL, 0);
        c->push = global(c, SpvStorageClassPushConstant, block, 0);
    }
    /* reads past the 64 dwords are clamped, as vulkan leaves them undefined. */
    uint32_t args[2] = {spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, byte_offset, uconst(c, 2)), uconst(c, 63)};
    uint32_t indices[2] = {uconst(c, 0), spv_glsl(&c->b, c->t_u32, GLSLstd450UMin, args, 2)};
    uint32_t ptr = spv_access(&c->b, spv_type_pointer(&c->b, SpvStorageClassPushConstant, c->t_u32), c->push, indices, 2);
    return spv_load(&c->b, c->t_u32, ptr);
}

/* the register an operand names: its range's first register in shader model 5.0, the second index in 5.1. */
static uint32_t operand_register(Compiler *c, const DxbcOperand *op, uint32_t lower)
{
    if (!c->sm51)
        return uconst(c, lower);
    return index_value(c, &op->index[1]);
}

/*
 * the heap index of a descriptor: the stage's table entry in direct3d 11,
 * the table's first index plus the register's place in it in direct3d 12.
 */
static uint32_t heap_index(Compiler *c, uint32_t type, uint32_t entry, uint32_t space, uint32_t lower,
                           const DxbcOperand *op)
{
    if (!c->options || !c->options->root_signature)
        return table_entry(c, entry);
    const DxbcRootBinding *b = root_binding(c, type, space, lower);
    if (!b)
        return uconst(c, 0);
    uint32_t reg = operand_register(c, op, lower);
    uint32_t from_base = spv_op2(&c->b, SpvOpISub, c->t_u32, reg, uconst(c, b->base));
    uint32_t start = b->source == DXBC_SOURCE_TABLE ? push_word(c, uconst(c, b->push_offset)) : uconst(c, 0);
    return spv_op2(&c->b, SpvOpIAdd, c->t_u32, spv_op2(&c->b, SpvOpIAdd, c->t_u32, start, uconst(c, b->offset)),
                   from_base);
}

/* the render targets' sample count: in the table for direct3d 11, known when compiled for direct3d 12. */
static uint32_t rasterizer_samples(Compiler *c)
{
    if (c->options && c->options->root_signature)
        return uconst(c, c->options->sample_count ? c->options->sample_count : 1);
    return table_entry(c, DXBC_TABLE_SAMPLE_COUNT);
}

/* marks a value as differing between invocations, for an index of the heap of a binding. */
static uint32_t nonuniform(Compiler *c, uint32_t id, uint32_t binding)
{
    static const uint32_t capabilities[] = {
        [DXBC_HEAP_SAMPLED_IMAGE] = SpvCapabilitySampledImageArrayNonUniformIndexing,
        [DXBC_HEAP_STORAGE_IMAGE] = SpvCapabilityStorageImageArrayNonUniformIndexing,
        [DXBC_HEAP_UNIFORM_TEXEL] = SpvCapabilityUniformTexelBufferArrayNonUniformIndexing,
        [DXBC_HEAP_STORAGE_TEXEL] = SpvCapabilityStorageTexelBufferArrayNonUniformIndexing,
        [DXBC_HEAP_SAMPLER] = SpvCapabilitySampledImageArrayNonUniformIndexing,
        [DXBC_HEAP_UNIFORM_BUFFER] = SpvCapabilityUniformBufferArrayNonUniformIndexing,
    };
    spv_capability(&c->b, SpvCapabilityShaderNonUniform);
    spv_capability(&c->b, capabilities[binding]);
    spv_decorate(&c->b, id, SpvDecorationNonUniform, NULL, 0);
    return id;
}

/* a pointer to the heap's descriptor at an index. */
static uint32_t descriptor_pointer(Compiler *c, uint32_t image_type, uint32_t binding, uint32_t index)
{
    uint32_t heap = image_heap(c, image_type, binding);
    uint32_t ptr_type = spv_type_pointer(&c->b, SpvStorageClassUniformConstant, image_type);
    return spv_access(&c->b, ptr_type, heap, &index, 1);
}

/* the heap's descriptor for a resource operand, marked when its index is not uniform. */
static uint32_t operand_descriptor(Compiler *c, const DxbcOperand *op, uint32_t type, uint32_t binding,
                                   uint32_t index)
{
    if (op->nonuniform)
        nonuniform(c, index, binding);
    uint32_t ptr = descriptor_pointer(c, type, binding, index);
    return op->nonuniform ? nonuniform(c, ptr, binding) : ptr;
}

static uint32_t load_srv(Compiler *c, const DxbcOperand *op, int depth, uint32_t *type)
{
    uint32_t binding, slot = op->index[0].immediate & 127;
    *type = srv_image_type(c, slot, depth, &binding);
    const Resource *r = &c->srv[slot];
    uint32_t index = heap_index(c, DXBC_BIND_SRV, DXBC_TABLE_SRV + slot, r->space, r->lower, op);
    uint32_t image = spv_load(&c->b, *type, operand_descriptor(c, op, *type, binding, index));
    return op->nonuniform ? nonuniform(c, image, binding) : image;
}

static uint32_t uav_pointer(Compiler *c, const DxbcOperand *op, uint32_t *type)
{
    uint32_t binding, slot = op->index[0].immediate & 63;
    *type = uav_image_type(c, slot, &binding);
    const Resource *r = &c->uav[slot];
    uint32_t index = heap_index(c, DXBC_BIND_UAV, DXBC_TABLE_UAV + slot, r->space, r->lower, op);
    return operand_descriptor(c, op, *type, binding, index);
}

/* a sampler of the heap, or of the root signature's static samplers. */
static uint32_t load_sampler(Compiler *c, const DxbcOperand *op)
{
    uint32_t type = spv_type_sampler(&c->b), slot = op->index[0].immediate & 15;
    uint32_t ptr_type = spv_type_pointer(&c->b, SpvStorageClassUniformConstant, type);
    if (c->options && c->options->root_signature) {
        const DxbcRootBinding *b = root_binding(c, DXBC_BIND_SAMPLER, c->sampler_space[slot], c->sampler_lower[slot]);
        if (b && b->source == DXBC_SOURCE_STATIC_SAMPLER) {
            if (!c->static_samplers) {
                c->static_samplers =
                    global(c, SpvStorageClassUniformConstant, spv_type_runtime_array(&c->b, type), 0);
                decorate1(c, c->static_samplers, SpvDecorationDescriptorSet, DXBC_ROOT_SET_STATIC_SAMPLERS);
                decorate1(c, c->static_samplers, SpvDecorationBinding, 0);
            }
            uint32_t from_base = spv_op2(&c->b, SpvOpISub, c->t_u32,
                                         operand_register(c, op, c->sampler_lower[slot]), uconst(c, b->base));
            uint32_t index = spv_op2(&c->b, SpvOpIAdd, c->t_u32, uconst(c, b->offset), from_base);
            return spv_load(&c->b, type, spv_access(&c->b, ptr_type, c->static_samplers, &index, 1));
        }
    }
    if (!c->sampler_heap) {
        int root = c->options && c->options->root_signature;
        c->sampler_heap = global(c, SpvStorageClassUniformConstant, spv_type_runtime_array(&c->b, type), 0);
        decorate1(c, c->sampler_heap, SpvDecorationDescriptorSet, root ? DXBC_ROOT_SET_SAMPLERS : DXBC_SET_HEAP);
        decorate1(c, c->sampler_heap, SpvDecorationBinding, root ? 0 : DXBC_HEAP_SAMPLER);
    }
    uint32_t index = heap_index(c, DXBC_BIND_SAMPLER, DXBC_TABLE_SAMPLER + slot, c->sampler_space[slot],
                                c->sampler_lower[slot], op);
    if (!op->nonuniform)
        return spv_load(&c->b, type, spv_access(&c->b, ptr_type, c->sampler_heap, &index, 1));
    nonuniform(c, index, DXBC_HEAP_SAMPLER);
    uint32_t ptr = nonuniform(c, spv_access(&c->b, ptr_type, c->sampler_heap, &index, 1), DXBC_HEAP_SAMPLER);
    return nonuniform(c, spv_load(&c->b, type, ptr), DXBC_HEAP_SAMPLER);
}

/* a device address, a uvec2, as a pointer of the type into physical storage. */
static uint32_t address_pointer(Compiler *c, uint32_t address, uint32_t pointer_type)
{
    spv_capability(&c->b, SpvCapabilityPhysicalStorageBufferAddresses);
    c->b.addressing = SpvAddressingModelPhysicalStorageBuffer64;
    return spv_op1(&c->b, SpvOpBitcast, pointer_type, address);
}

/* the device address of a root descriptor, in the push constants. */
static uint32_t root_address(Compiler *c, const DxbcRootBinding *b)
{
    uint32_t parts[2] = {push_word(c, uconst(c, b->push_offset)), push_word(c, uconst(c, b->push_offset + 4))};
    return spv_op(&c->b, SpvOpCompositeConstruct, c->t_vec[K_UINT][2], parts, 2);
}

/* half of the record of a heap descriptor: address, size and stride, then the counter's address. */
static uint32_t heap_record(Compiler *c, uint32_t index, uint32_t half)
{
    uint32_t v4 = c->t_vec[K_UINT][4];
    if (!c->records) {
        uint32_t array = spv_type_runtime_array(&c->b, v4);
        decorate1(c, array, SpvDecorationArrayStride, 16);
        uint32_t block = spv_type_struct(&c->b, &array, 1);
        uint32_t offset = 0;
        spv_member_decorate(&c->b, block, 0, SpvDecorationOffset, &offset, 1);
        spv_member_decorate(&c->b, block, 0, SpvDecorationNonWritable, NULL, 0);
        spv_decorate(&c->b, block, SpvDecorationBlock, NULL, 0);
        c->records = global(c, SpvStorageClassStorageBuffer, block, 0);
        decorate1(c, c->records, SpvDecorationDescriptorSet, DXBC_ROOT_SET_RECORDS);
        decorate1(c, c->records, SpvDecorationBinding, 0);
    }
    uint32_t at = spv_op2(&c->b, SpvOpIAdd, c->t_u32, spv_op2(&c->b, SpvOpShiftLeftLogical, c->t_u32, index, uconst(c, 1)),
                          uconst(c, half));
    uint32_t indices[2] = {uconst(c, 0), at};
    return spv_load(&c->b, v4,
                    spv_access(&c->b, spv_type_pointer(&c->b, SpvStorageClassStorageBuffer, v4), c->records, indices, 2));
}

/* two components of a uvec4 as a uvec2. */
static uint32_t uvec2_of(Compiler *c, uint32_t v, uint32_t first)
{
    uint32_t w[4] = {v, v, first, first + 1};
    return spv_op(&c->b, SpvOpVectorShuffle, c->t_vec[K_UINT][2], w, 4);
}

/* the type of a pointer to an array of what a root descriptor holds, vec4 for constants, words for the rest. */
static uint32_t root_array_pointer(Compiler *c, uint32_t element, uint32_t stride)
{
    uint32_t *cached = stride == 16 ? &c->root_vec4_pointer : &c->root_word_pointer;
    if (*cached)
        return *cached;
    uint32_t array = spv_type_runtime_array_unique(&c->b, element);
    decorate1(c, array, SpvDecorationArrayStride, stride);
    uint32_t block = spv_type_struct_unique(&c->b, &array, 1);
    uint32_t offset = 0;
    spv_member_decorate(&c->b, block, 0, SpvDecorationOffset, &offset, 1);
    spv_decorate(&c->b, block, SpvDecorationBlock, NULL, 0);
    return *cached = spv_type_pointer(&c->b, SpvStorageClassPhysicalStorageBuffer, block);
}

/* a loaded vec4 of a constant buffer, the element a uint value: from the heap, a device address or push constants. */
static uint32_t cb_load(Compiler *c, uint32_t slot, uint32_t element, const DxbcOperand *op)
{
    uint32_t v4 = c->t_vec[K_FLOAT][4];
    uint32_t space = slot < 32 ? c->cb_space[slot] : 0, lower = slot < 32 ? c->cb_lower[slot] : slot;
    if (c->options && c->options->root_signature) {
        const DxbcRootBinding *b = root_binding(c, DXBC_BIND_CBV, space, lower);
        if (!b)
            return splat_const(c, K_FLOAT, 4, 0);
        if (b->source == DXBC_SOURCE_CONSTANTS) {
            uint32_t base = spv_op2(&c->b, SpvOpIAdd, c->t_u32, uconst(c, b->push_offset),
                                    spv_op2(&c->b, SpvOpShiftLeftLogical, c->t_u32, element, uconst(c, 4)));
            uint32_t parts[4];
            for (uint32_t i = 0; i < 4; i++)
                parts[i] = push_word(c, spv_op2(&c->b, SpvOpIAdd, c->t_u32, base, uconst(c, 4 * i)));
            return spv_op1(&c->b, SpvOpBitcast, v4, spv_op(&c->b, SpvOpCompositeConstruct, c->t_vec[K_UINT][4], parts, 4));
        }
        /* a table's views are read through their records, out of bounds reads giving zero. */
        uint32_t address, in = 0;
        if (b->source == DXBC_SOURCE_ADDRESS) {
            address = root_address(c, b);
        } else {
            uint32_t record = heap_record(c, heap_index(c, DXBC_BIND_CBV, 0, space, lower, op), 0);
            address = uvec2_of(c, record, 0);
            uint32_t size = spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, extract(c, c->t_u32, record, 2), uconst(c, 4));
            in = spv_op2(&c->b, SpvOpULessThan, c->t_bool, element, size);
            element = spv_op3(&c->b, SpvOpSelect, c->t_u32, in, element, uconst(c, 0));
        }
        uint32_t ptr = address_pointer(c, address, root_array_pointer(c, v4, 16));
        uint32_t indices[2] = {uconst(c, 0), element};
        uint32_t elem =
            spv_access(&c->b, spv_type_pointer(&c->b, SpvStorageClassPhysicalStorageBuffer, v4), ptr, indices, 2);
        uint32_t w[3] = {elem, SpvMemoryAccessAlignedMask, 16};
        uint32_t v = spv_op(&c->b, SpvOpLoad, v4, w, 3);
        if (!in)
            return v;
        uint32_t cond = spv_op(&c->b, SpvOpCompositeConstruct, c->t_bvec[4], (uint32_t[]){in, in, in, in}, 4);
        return spv_op3(&c->b, SpvOpSelect, v4, cond, v, splat_const(c, K_FLOAT, 4, 0));
    }
    if (!c->cb_heap) {
        uint32_t array = spv_type_array_unique(&c->b, v4, 4096);
        decorate1(c, array, SpvDecorationArrayStride, 16);
        uint32_t block = spv_type_struct_unique(&c->b, &array, 1);
        uint32_t offset = 0;
        spv_member_decorate(&c->b, block, 0, SpvDecorationOffset, &offset, 1);
        spv_decorate(&c->b, block, SpvDecorationBlock, NULL, 0);
        c->cb_heap = global(c, SpvStorageClassUniform, spv_type_runtime_array(&c->b, block), 0);
        decorate1(c, c->cb_heap, SpvDecorationDescriptorSet, DXBC_SET_HEAP);
        decorate1(c, c->cb_heap, SpvDecorationBinding, DXBC_HEAP_UNIFORM_BUFFER);
    }
    uint32_t index = heap_index(c, DXBC_BIND_CBV, DXBC_TABLE_CB + (slot & 15), space, lower, op);
    if (op->nonuniform)
        nonuniform(c, index, DXBC_HEAP_UNIFORM_BUFFER);
    uint32_t indices[3] = {index, uconst(c, 0), element};
    uint32_t ptr = spv_access(&c->b, spv_type_pointer(&c->b, SpvStorageClassUniform, v4), c->cb_heap, indices, 3);
    if (op->nonuniform)
        nonuniform(c, ptr, DXBC_HEAP_UNIFORM_BUFFER);
    return spv_load(&c->b, v4, ptr);
}

/*
 * a raw or structured view of direct3d 12, read and written through its
 * device address: a root descriptor's, unchecked, or a table's, whose
 * record gives the size in bytes and the counter's address.
 */
typedef struct RootBuffer {
    uint32_t address;
    uint32_t size;
    uint32_t counter;
} RootBuffer;

static int root_buffer(Compiler *c, const DxbcOperand *resource, RootBuffer *out)
{
    if (!c->options || !c->options->root_signature)
        return 0;
    int uav = resource->type == OPERAND_UNORDERED_ACCESS_VIEW;
    if (!uav && resource->type != OPERAND_RESOURCE)
        return 0;
    const Resource *r = uav ? &c->uav[resource->index[0].immediate & 63] : &c->srv[resource->index[0].immediate & 127];
    if (!r->raw && !r->structured)
        return 0;
    uint32_t type = uav ? DXBC_BIND_UAV : DXBC_BIND_SRV;
    const DxbcRootBinding *b = root_binding(c, type, r->space, r->lower);
    if (!b)
        return 0;
    if (b->source == DXBC_SOURCE_ADDRESS) {
        *out = (RootBuffer){root_address(c, b), 0, 0};
        return 1;
    }
    uint32_t index = heap_index(c, type, 0, r->space, r->lower, resource);
    uint32_t record = heap_record(c, index, 0);
    out->address = uvec2_of(c, record, 0);
    out->size = extract(c, c->t_u32, record, 2);
    out->counter = uav ? uvec2_of(c, heap_record(c, index, 1), 0) : 0;
    return 1;
}

/* whether a word is inside a view with a record. */
static uint32_t word_in_bounds(Compiler *c, const RootBuffer *rb, uint32_t word)
{
    uint32_t words = spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, rb->size, uconst(c, 2));
    return spv_op2(&c->b, SpvOpULessThan, c->t_bool, word, words);
}

/* a pointer to a word of a buffer at a device address. */
static uint32_t root_word_pointer(Compiler *c, uint32_t address, uint32_t word)
{
    uint32_t ptr = address_pointer(c, address, root_array_pointer(c, c->t_u32, 4));
    uint32_t indices[2] = {uconst(c, 0), word};
    return spv_access(&c->b, spv_type_pointer(&c->b, SpvStorageClassPhysicalStorageBuffer, c->t_u32), ptr, indices, 2);
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
        if (!c->inputs) {
            fail(c, "input read without declared inputs");
            return 0;
        }
        if (op->index_count == 2) {
            uint32_t i[2] = {index_value(c, &op->index[0]), index_value(c, &op->index[1])};
            return spv_access(&c->b, ptr, c->inputs, i, 2);
        }
        uint32_t i = index_value(c, &op->index[0]);
        return spv_access(&c->b, ptr, c->inputs, &i, 1);
    }
    case OPERAND_OUTPUT: {
        uint32_t i = index_value(c, &op->index[0]);
        /* the fork and join phases of hull shaders write patch constants. */
        if (c->phase == PHASE_FORK || c->phase == PHASE_JOIN) {
            if (!c->patch_regs) {
                fail(c, "patch constant written without its declaration");
                return 0;
            }
            return spv_access(&c->b, ptr, c->patch_regs, &i, 1);
        }
        if (!c->outputs) {
            fail(c, "output written without declared outputs");
            return 0;
        }
        return spv_access(&c->b, ptr, c->outputs, &i, 1);
    }
    case OPERAND_INPUT_CONTROL_POINT: {
        if (!c->inputs) {
            fail(c, "control point read without declared inputs");
            return 0;
        }
        uint32_t i[2] = {index_value(c, &op->index[0]), index_value(c, &op->index[1])};
        return spv_access(&c->b, ptr, c->inputs, i, 2);
    }
    case OPERAND_INPUT_PATCH_CONSTANT: {
        if (!c->patch_regs) {
            fail(c, "patch constant read without its declaration");
            return 0;
        }
        uint32_t i = index_value(c, &op->index[0]);
        return spv_access(&c->b, ptr, c->patch_regs, &i, 1);
    }
    case OPERAND_OUTPUT_CONTROL_POINT: {
        uint32_t reg = op->index[1].immediate;
        if (reg >= MAX_REGS || !c->cp_outputs[reg]) {
            fail(c, "output control point read without its declaration");
            return 0;
        }
        uint32_t i = index_value(c, &op->index[0]);
        return spv_access(&c->b, spv_type_pointer(&c->b, SpvStorageClassOutput, vec4), c->cp_outputs[reg], &i, 1);
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
    case OPERAND_OUTPUT_CONTROL_POINT_ID: {
        uint32_t s = spv_load(&c->b, c->t_i32, builtin(c, SpvBuiltInInvocationId, SpvStorageClassInput, c->t_i32));
        return splat(c, K_UINT, 4, spv_op1(&c->b, SpvOpBitcast, c->t_u32, s));
    }
    case OPERAND_INPUT_FORK_INSTANCE_ID:
    case OPERAND_INPUT_JOIN_INSTANCE_ID:
        if (!c->phase_instance) {
            fail(c, "phase instance outside a hull shader");
            return splat_const(c, K_UINT, 4, 0);
        }
        return splat(c, K_UINT, 4, spv_load(&c->b, c->t_u32, c->phase_instance));
    case OPERAND_INPUT_DOMAIN_POINT: {
        *kind = K_FLOAT;
        uint32_t v3 = c->t_vec[K_FLOAT][3];
        uint32_t v = spv_load(&c->b, v3, builtin(c, SpvBuiltInTessCoord, SpvStorageClassInput, v3));
        uint32_t parts[4] = {extract(c, c->t_f32, v, 0), extract(c, c->t_f32, v, 1), extract(c, c->t_f32, v, 2),
                             fconst(c, 0.0f)};
        return spv_op(&c->b, SpvOpCompositeConstruct, c->t_vec[K_FLOAT][4], parts, 4);
    }
    case OPERAND_INPUT_GS_INSTANCE_ID: {
        uint32_t s = spv_load(&c->b, c->t_i32, builtin(c, SpvBuiltInInvocationId, SpvStorageClassInput, c->t_i32));
        return splat(c, K_UINT, 4, spv_op1(&c->b, SpvOpBitcast, c->t_u32, s));
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
    case OPERAND_OUTPUT:
    case OPERAND_INPUT_CONTROL_POINT:
    case OPERAND_INPUT_PATCH_CONSTANT:
    case OPERAND_OUTPUT_CONTROL_POINT: {
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
        if (slot >= (c->sm51 ? 32u : 14u)) {
            fail(c, "constant buffer slot out of range");
            return splat_const(c, K_FLOAT, 4, 0);
        }
        return cb_load(c, slot, index_value(c, &op->index[c->sm51 ? 2 : 1]), op);
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
static uint32_t begin_conditional(Compiler *c, uint32_t cond, uint32_t *merge);

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
    int output = storage == SpvStorageClassOutput;
    if (which < 64 && c->builtins[output][which])
        return c->builtins[output][which];
    uint32_t var = global(c, storage, type, 0);
    decorate1(c, var, SpvDecorationBuiltIn, which);
    if (output && c->program == DXBC_GEOMETRY && c->raster_stream)
        decorate1(c, var, SpvDecorationStream, c->raster_stream);
    if (storage == SpvStorageClassInput && c->program == DXBC_PIXEL && type != c->t_bool &&
        (which == SpvBuiltInPrimitiveId || which == SpvBuiltInSampleId || which == SpvBuiltInLayer ||
         which == SpvBuiltInViewportIndex))
        spv_decorate(&c->b, var, SpvDecorationFlat, NULL, 0);
    if (which < 64)
        c->builtins[output][which] = var;
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

/*
 * the inputs of geometry, hull and domain shaders: an array per register,
 * a vec4 per vertex of the primitive or control point of the patch.
 */
static void load_vertex_inputs(Compiler *c, uint32_t vertices)
{
    uint32_t v4 = c->t_vec[K_FLOAT][4];
    uint32_t vptr = spv_type_pointer(&c->b, SpvStorageClassPrivate, v4);
    uint32_t fptr = spv_type_pointer(&c->b, SpvStorageClassPrivate, c->t_f32);
    uint32_t array = spv_type_array(&c->b, v4, vertices);
    uint32_t in_ptr = spv_type_pointer(&c->b, SpvStorageClassInput, v4);
    for (uint32_t reg = 0; reg < c->input_count; reg++) {
        InputDecl *d = &c->input_decl[reg];
        if (!d->declared)
            continue;
        uint32_t var;
        uint8_t mask = d->mask | d->sv_mask;
        if (d->sv == SV_POSITION) {
            var = builtin(c, SpvBuiltInPosition, SpvStorageClassInput, array);
        } else if (d->sv == SV_UNDEFINED) {
            var = global(c, SpvStorageClassInput, array, 0);
            decorate1(c, var, SpvDecorationLocation, reg);
        } else {
            fail(c, "per vertex input system value not implemented");
            return;
        }
        d->var = var;
        for (uint32_t v = 0; v < vertices; v++) {
            uint32_t vertex = uconst(c, v), index = uconst(c, reg);
            uint32_t value = spv_load(&c->b, v4, spv_access(&c->b, in_ptr, var, &vertex, 1));
            uint32_t at[2] = {vertex, index};
            uint32_t base = spv_access(&c->b, vptr, c->inputs, at, 2);
            if (mask == 0xF) {
                spv_store(&c->b, base, value);
                continue;
            }
            for (uint32_t i = 0; i < 4; i++)
                if (mask & (1 << i)) {
                    uint32_t ci = uconst(c, i);
                    spv_store(&c->b, spv_access(&c->b, fptr, base, &ci, 1), extract(c, c->t_f32, value, i));
                }
        }
    }
}

/*
 * the system value a declaration names. instructions name each edge and
 * inside factor apart, 11 to 22, where signatures and SV_ name kinds of
 * factors, 11 to 16.
 */
static uint32_t token_sv(uint32_t name)
{
    if (name < 11 || name > 22)
        return name;
    if (name <= 14)
        return SV_FINAL_QUAD_EDGE_TESSFACTOR;
    if (name <= 16)
        return SV_FINAL_QUAD_INSIDE_TESSFACTOR;
    if (name <= 19)
        return SV_FINAL_TRI_EDGE_TESSFACTOR;
    return name == 20 ? SV_FINAL_TRI_INSIDE_TESSFACTOR
                      : name == 21 ? SV_FINAL_LINE_DETAIL_TESSFACTOR : SV_FINAL_LINE_DENSITY_TESSFACTOR;
}

/* the tessellation factor a patch constant system value is, as the index of the outer or inner level. */
static int tess_factor(Compiler *c, uint32_t sv, uint32_t reg, uint8_t mask, int *inner)
{
    const DxbcSignatureElement *e = signature_element(&c->info->patch_constants, reg, mask);
    uint32_t index = e ? e->semantic_index : 0;
    *inner = sv == SV_FINAL_QUAD_INSIDE_TESSFACTOR || sv == SV_FINAL_TRI_INSIDE_TESSFACTOR;
    switch (sv) {
    case SV_FINAL_QUAD_EDGE_TESSFACTOR:
    case SV_FINAL_TRI_EDGE_TESSFACTOR:
    case SV_FINAL_QUAD_INSIDE_TESSFACTOR:
        return (int)index;
    case SV_FINAL_TRI_INSIDE_TESSFACTOR:
        return 0;
    /* isolines: vulkan counts the lines in the first level, their segments in the second. */
    case SV_FINAL_LINE_DETAIL_TESSFACTOR:
        return 1;
    case SV_FINAL_LINE_DENSITY_TESSFACTOR:
        return 0;
    default:
        return -1;
    }
}

static uint32_t tess_level(Compiler *c, int inner, uint32_t storage)
{
    uint32_t array = spv_type_array(&c->b, c->t_f32, inner ? 2 : 4);
    uint32_t which = inner ? SpvBuiltInTessLevelInner : SpvBuiltInTessLevelOuter;
    int fresh = !c->builtins[storage == SpvStorageClassOutput][which];
    uint32_t var = builtin(c, which, storage, array);
    if (fresh)
        spv_decorate(&c->b, var, SpvDecorationPatch, NULL, 0);
    return var;
}

/* the patch constants of a hull shader: tessellation factors and patch outputs. */
static void store_patch_constants(Compiler *c)
{
    uint32_t v4 = c->t_vec[K_FLOAT][4];
    uint32_t vptr = spv_type_pointer(&c->b, SpvStorageClassPrivate, v4);
    uint32_t optr = spv_type_pointer(&c->b, SpvStorageClassOutput, c->t_f32);
    for (uint32_t reg = 0; reg < c->patch_count; reg++) {
        InputDecl *d = &c->patch_decl[reg];
        if (!d->declared)
            continue;
        uint32_t index = uconst(c, reg);
        uint32_t value = spv_load(&c->b, v4, spv_access(&c->b, vptr, c->patch_regs, &index, 1));
        if (d->sv_mask) {
            /* each component of the register is a factor of its own, as the signature numbers them. */
            for (uint32_t i = 0; i < 4; i++) {
                if (!(d->sv_mask & (1 << i)))
                    continue;
                int inner;
                int at = tess_factor(c, d->sv, reg, (uint8_t)(1 << i), &inner);
                if (at < 0)
                    continue;
                uint32_t level = tess_level(c, inner, SpvStorageClassOutput);
                uint32_t k = uconst(c, (uint32_t)at);
                uint32_t factor = extract(c, c->t_f32, value, i);
                /* the tessellator takes factors up to the hull shader's maximum. */
                if (c->max_tess_factor) {
                    uint32_t args[2] = {factor, fconst_bits(c, c->max_tess_factor)};
                    factor = spv_glsl(&c->b, c->t_f32, GLSLstd450NMin, args, 2);
                }
                spv_store(&c->b, spv_access(&c->b, optr, level, &k, 1), factor);
            }
        }
        if (d->mask) {
            if (!d->var) {
                d->var = global(c, SpvStorageClassOutput, v4, 0);
                decorate1(c, d->var, SpvDecorationLocation, reg);
                spv_decorate(&c->b, d->var, SpvDecorationPatch, NULL, 0);
            }
            spv_store(&c->b, d->var, value);
        }
    }
}

/* the patch constants a domain shader reads: patch inputs and the tessellation factors. */
static void load_patch_constants(Compiler *c)
{
    uint32_t v4 = c->t_vec[K_FLOAT][4];
    uint32_t vptr = spv_type_pointer(&c->b, SpvStorageClassPrivate, v4);
    uint32_t fptr = spv_type_pointer(&c->b, SpvStorageClassPrivate, c->t_f32);
    uint32_t iptr = spv_type_pointer(&c->b, SpvStorageClassInput, c->t_f32);
    for (uint32_t reg = 0; reg < c->patch_count; reg++) {
        InputDecl *d = &c->patch_decl[reg];
        if (!d->declared)
            continue;
        uint32_t index = uconst(c, reg);
        uint32_t base = spv_access(&c->b, vptr, c->patch_regs, &index, 1);
        if (d->mask) {
            uint32_t var = global(c, SpvStorageClassInput, v4, 0);
            decorate1(c, var, SpvDecorationLocation, reg);
            spv_decorate(&c->b, var, SpvDecorationPatch, NULL, 0);
            spv_store(&c->b, base, spv_load(&c->b, v4, var));
        }
        for (uint32_t i = 0; i < 4; i++) {
            if (!(d->sv_mask & (1 << i)))
                continue;
            int inner;
            int at = tess_factor(c, d->sv, reg, (uint8_t)(1 << i), &inner);
            if (at < 0)
                continue;
            uint32_t level = tess_level(c, inner, SpvStorageClassInput);
            uint32_t k = uconst(c, (uint32_t)at), ci = uconst(c, i);
            uint32_t v = spv_load(&c->b, c->t_f32, spv_access(&c->b, iptr, level, &k, 1));
            spv_store(&c->b, spv_access(&c->b, fptr, base, &ci, 1), v);
        }
    }
}

/*
 * the hull shader: each invocation runs the control point phase for its
 * control point, or copies its input through without one; after a
 * barrier the first runs the fork and join phases, each instance in turn,
 * and writes the patch constants.
 */
static void hull_main(Compiler *c, uint32_t cp_fn)
{
    SpvBuilder *b = &c->b;
    uint32_t v4 = c->t_vec[K_FLOAT][4];
    uint32_t vptr = spv_type_pointer(&c->b, SpvStorageClassPrivate, v4);
    uint32_t optr = spv_type_pointer(&c->b, SpvStorageClassOutput, v4);
    uint32_t id = spv_load(b, c->t_i32, builtin(c, SpvBuiltInInvocationId, SpvStorageClassInput, c->t_i32));
    uint32_t invocation = spv_op1(b, SpvOpBitcast, c->t_u32, id);
    if (c->has_cp_phase)
        spv_op(b, SpvOpFunctionCall, c->t_void, &cp_fn, 1);
    for (uint32_t reg = 0; reg < MAX_REGS; reg++) {
        if (!c->cp_outputs[reg])
            continue;
        uint32_t value;
        uint32_t index = uconst(c, reg);
        if (c->has_cp_phase) {
            value = spv_load(b, v4, spv_access(b, vptr, c->outputs, &index, 1));
        } else {
            uint32_t at[2] = {invocation, index};
            value = c->inputs && reg < c->input_count ? spv_load(b, v4, spv_access(b, vptr, c->inputs, at, 2))
                                                      : splat_const(c, K_FLOAT, 4, 0);
        }
        spv_store(b, spv_access(b, optr, c->cp_outputs[reg], &invocation, 1), value);
    }
    uint32_t barrier[3] = {uconst(c, SpvScopeWorkgroup), uconst(c, SpvScopeInvocation), uconst(c, 0)};
    spv_op_void(b, SpvOpControlBarrier, barrier, 3);
    uint32_t merge;
    begin_conditional(c, spv_op2(b, SpvOpIEqual, c->t_bool, invocation, uconst(c, 0)), &merge);
    int saved = c->phase;
    for (uint32_t i = 0; i < c->phase_count; i++)
        for (uint32_t instance = 0; instance < c->phases[i].instances; instance++) {
            spv_store(b, c->phase_instance, uconst(c, instance));
            spv_op(b, SpvOpFunctionCall, c->t_void, &c->phases[i].fn, 1);
        }
    c->phase = PHASE_FORK;
    store_patch_constants(c);
    c->phase = saved;
    spv_branch(b, merge);
    spv_label(b, merge);
}

/* the output control points of a hull shader: an array per register, a vec4 per control point. */
static void make_control_point_outputs(Compiler *c)
{
    uint32_t array = spv_type_array(&c->b, c->t_vec[K_FLOAT][4], c->cp_out ? c->cp_out : 1);
    for (uint32_t reg = 0; reg < MAX_REGS; reg++) {
        int declared = c->has_cp_phase ? c->output_decl[reg].declared
                                       : signature_element(&c->info->outputs, reg, 0xF) != NULL;
        if (!declared)
            continue;
        c->cp_outputs[reg] = global(c, SpvStorageClassOutput, array, 0);
        decorate1(c, c->cp_outputs[reg], SpvDecorationLocation, reg);
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
                if (c->program == DXBC_GEOMETRY && c->raster_stream)
                    decorate1(c, d->var, SpvDecorationStream, c->raster_stream);
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

static int same_name(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a, y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
        if (x != y)
            return 0;
        if (!x)
            return 1;
    }
}

/*
 * the output variables stream output captures: one per declaration entry,
 * past the registers, with its buffer, the buffer's stride and its offset
 * there. entries without a name are gaps in their buffer.
 */
static void declare_captures(Compiler *c)
{
    const DxbcCompileOptions *o = c->options;
    c->captures_declared = 1;
    if (!o || !o->so_count)
        return;
    c->captures = mime_alloc(c->alloc, o->so_count * sizeof(*c->captures));
    if (!c->captures)
        return;
    uint32_t location = c->output_count;
    for (uint32_t i = 0; i < c->info->outputs.count; i++)
        if (c->info->outputs.elements[i].reg + 1 > location && c->info->outputs.elements[i].reg < 32)
            location = c->info->outputs.elements[i].reg + 1;
    uint32_t offsets[4] = {0}, strides[4] = {0};
    for (uint32_t i = 0; i < o->so_count; i++)
        if (o->so[i].output_slot < 4)
            strides[o->so[i].output_slot] += o->so[i].component_count * 4;
    for (uint32_t i = 0; i < o->so_stride_count && i < 4; i++)
        strides[i] = o->so_strides[i];
    spv_capability(&c->b, SpvCapabilityTransformFeedback);
    for (uint32_t i = 0; i < o->so_count; i++) {
        const DxbcStreamOutput *so = &o->so[i];
        uint32_t slot = so->output_slot & 3, offset = offsets[slot];
        offsets[slot] += so->component_count * 4;
        if (!so->semantic_name || !so->component_count)
            continue;
        const DxbcSignatureElement *e = NULL;
        for (uint32_t k = 0; k < c->info->outputs.count && !e; k++) {
            const DxbcSignatureElement *x = &c->info->outputs.elements[k];
            if (x->stream == so->stream && x->semantic_index == so->semantic_index && same_name(x->name, so->semantic_name))
                e = x;
        }
        if (!e || !e->mask)
            continue;
        struct Capture *cap = &c->captures[c->capture_count++];
        cap->reg = e->reg;
        cap->stream = so->stream;
        cap->first = (uint8_t)(__builtin_ctz(e->mask) + so->start_component);
        cap->count = so->component_count;
        if (cap->first + cap->count > 4)
            cap->count = (uint8_t)(cap->first < 4 ? 4 - cap->first : 0);
        cap->kind = component_kind(e->component_type);
        uint32_t type = cap->count == 1 ? vec_type(c, cap->kind, 1) : vec_type(c, cap->kind, cap->count);
        cap->var = global(c, SpvStorageClassOutput, type, 0);
        decorate1(c, cap->var, SpvDecorationLocation, location++);
        decorate1(c, cap->var, SpvDecorationXfbBuffer, slot);
        decorate1(c, cap->var, SpvDecorationXfbStride, strides[slot]);
        decorate1(c, cap->var, SpvDecorationOffset, offset);
        if (so->stream) {
            spv_capability(&c->b, SpvCapabilityGeometryStreams);
            decorate1(c, cap->var, SpvDecorationStream, so->stream);
        }
    }
}

/* the captured components of the outputs, before an emit to the stream. */
static void store_captures(Compiler *c, uint32_t stream)
{
    if (!c->captures_declared)
        declare_captures(c);
    uint32_t v4 = c->t_vec[K_FLOAT][4];
    uint32_t vptr = spv_type_pointer(&c->b, SpvStorageClassPrivate, v4);
    for (uint32_t i = 0; i < c->capture_count; i++) {
        struct Capture *cap = &c->captures[i];
        if (cap->stream != stream || !cap->count || cap->reg >= c->output_count)
            continue;
        uint32_t index = uconst(c, cap->reg);
        uint32_t reg = bitcast(c, spv_load(&c->b, v4, spv_access(&c->b, vptr, c->outputs, &index, 1)), K_FLOAT,
                               cap->kind, 4);
        uint32_t parts[4];
        for (uint32_t k = 0; k < cap->count; k++)
            parts[k] = extract(c, vec_type(c, cap->kind, 1), reg, cap->first + k);
        uint32_t value = cap->count == 1 ? parts[0]
                                         : spv_op(&c->b, SpvOpCompositeConstruct, vec_type(c, cap->kind, cap->count),
                                                  parts, cap->count);
        spv_store(&c->b, cap->var, value);
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
            /* each hull shader phase declares its own: they share the largest set. */
            if (x[0] > c->temp_count)
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
        case OP_DCL_CONSTANT_BUFFER: {
            /* shader model 5.1 names a range of registers and its space: cb id[lower:upper][size], space. */
            uint32_t id = op->index[0].immediate;
            if (c->sm51) {
                if (id >= 32 || ins.extra_count < 2)
                    return fail(c, "bad constant buffer declaration");
                c->cb_lower[id] = op->index[1].immediate;
                c->cb_space[id] = x[1];
            } else if (id < 32) {
                c->cb_lower[id] = id;
            }
            if (id < 14)
                c->info->cb_mask |= 1u << id;
            break;
        }
        case OP_DCL_SAMPLER: {
            uint32_t id = op->index[0].immediate;
            if (id >= 16)
                return fail(c, "sampler slot out of range");
            c->info->sampler_mask |= 1u << id;
            c->sampler_mode[id] = TOK_SAMPLER_MODE(ins.token);
            c->sampler_lower[id] = c->sm51 ? op->index[1].immediate : id;
            c->sampler_space[id] = c->sm51 && ins.extra_count ? x[0] : 0;
            break;
        }
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
            s->lower = c->sm51 ? op->index[1].immediate : slot;
            s->space = c->sm51 && ins.extra_count ? x[ins.extra_count - 1] : 0;
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
            u->lower = c->sm51 ? op->index[1].immediate : slot;
            u->space = c->sm51 && ins.extra_count ? x[ins.extra_count - 1] : 0;
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
            if (op->type != OPERAND_INPUT && op->type != OPERAND_INPUT_CONTROL_POINT &&
                op->type != OPERAND_INPUT_PATCH_CONSTANT) {
                /* system value operands, read where they are used. */
                break;
            }
            uint32_t reg = op->index[op->index_count ? op->index_count - 1 : 0].immediate;
            if (reg >= MAX_REGS)
                return fail(c, "input register out of range");
            int patch = op->type == OPERAND_INPUT_PATCH_CONSTANT;
            /* the hull shader reads its patch constants from its own registers. */
            if (patch && c->program == DXBC_HULL)
                break;
            InputDecl *d = patch ? &c->patch_decl[reg] : &c->input_decl[reg];
            if (patch && reg + 1 > c->patch_count)
                c->patch_count = reg + 1;
            d->declared = 1;
            uint32_t sv = SV_UNDEFINED;
            if (ins.opcode == OP_DCL_INPUT_SGV || ins.opcode == OP_DCL_INPUT_SIV || ins.opcode == OP_DCL_INPUT_PS_SGV ||
                ins.opcode == OP_DCL_INPUT_PS_SIV)
                sv = ins.extra_count ? token_sv(x[0]) : SV_UNDEFINED;
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
            if (!patch && reg + 1 > c->input_count)
                c->input_count = reg + 1;
            if (!patch)
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
            if (c->phase == PHASE_FORK || c->phase == PHASE_JOIN) {
                InputDecl *p = &c->patch_decl[reg];
                p->declared = 1;
                if (ins.opcode != OP_DCL_OUTPUT && ins.extra_count) {
                    p->sv = token_sv(x[0]);
                    p->sv_mask |= op->mask;
                } else {
                    p->mask |= op->mask;
                }
                if (reg + 1 > c->patch_count)
                    c->patch_count = reg + 1;
                break;
            }
            OutputDecl *d = &c->output_decl[reg];
            d->declared = 1;
            d->mask |= op->mask;
            if (ins.opcode != OP_DCL_OUTPUT && ins.extra_count)
                d->sv = token_sv(x[0]);
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
        case OP_DCL_GS_INPUT_PRIMITIVE: {
            /* point, line, triangle, line with adjacency, triangle with adjacency. */
            static const uint32_t VERTICES[8] = {0, 1, 2, 3, 0, 0, 4, 6};
            c->gs_primitive = (ins.token >> 11) & 0x3F;
            c->gs_vertices = c->gs_primitive < 8 ? VERTICES[c->gs_primitive] : 0;
            if (!c->gs_vertices)
                return fail(c, "geometry shader input primitive not implemented");
            c->info->gs_input_primitive = c->gs_primitive;
            break;
        }
        case OP_DCL_GS_OUTPUT_TOPOLOGY:
            c->gs_topology = (ins.token >> 11) & 0x7F;
            c->info->gs_output_topology = c->gs_topology;
            break;
        case OP_DCL_MAX_OUTPUT_VERTEX_COUNT:
            c->gs_max_vertices = ins.extra_count ? x[0] : 1;
            c->info->gs_max_vertices = c->gs_max_vertices;
            break;
        case OP_DCL_GS_INSTANCE_COUNT:
            c->gs_instances = ins.extra_count ? x[0] : 1;
            c->info->gs_instances = c->gs_instances;
            break;
        case OP_DCL_STREAM:
            c->gs_streams |= 1u << (op->index[0].immediate & 3);
            break;
        case OP_HS_DECLS:
            c->phase = PHASE_DECLS;
            break;
        case OP_HS_CONTROL_POINT_PHASE:
        case OP_HS_FORK_PHASE:
        case OP_HS_JOIN_PHASE: {
            uint32_t here = (uint32_t)(ins.words - c->code);
            /* the phase before ends here. */
            if (c->phase == PHASE_CONTROL_POINT)
                c->cp_end = here;
            else if ((c->phase == PHASE_FORK || c->phase == PHASE_JOIN) && c->phase_count)
                c->phases[c->phase_count - 1].end = here;
            if (ins.opcode == OP_HS_CONTROL_POINT_PHASE) {
                c->phase = PHASE_CONTROL_POINT;
                c->has_cp_phase = 1;
                c->cp_start = here + ins.length;
                c->cp_end = c->code_count;
            } else {
                if (c->phase_count >= 32)
                    return fail(c, "too many hull shader phases");
                c->phase = ins.opcode == OP_HS_FORK_PHASE ? PHASE_FORK : PHASE_JOIN;
                c->phases[c->phase_count].start = here + ins.length;
                c->phases[c->phase_count].end = c->code_count;
                c->phases[c->phase_count].instances = 1;
                c->phases[c->phase_count].fn = spv_id(&c->b);
                c->phase_count++;
            }
            break;
        }
        case OP_DCL_HS_FORK_PHASE_INSTANCE_COUNT:
        case OP_DCL_HS_JOIN_PHASE_INSTANCE_COUNT:
            if (c->phase_count && ins.extra_count)
                c->phases[c->phase_count - 1].instances = x[0];
            break;
        case OP_DCL_INPUT_CONTROL_POINT_COUNT:
            c->cp_in = (ins.token >> 11) & 0x3F;
            c->info->input_control_points = c->cp_in;
            break;
        case OP_DCL_OUTPUT_CONTROL_POINT_COUNT:
            c->cp_out = (ins.token >> 11) & 0x3F;
            c->info->output_control_points = c->cp_out;
            break;
        case OP_DCL_TESS_DOMAIN:
            c->tess_domain = (ins.token >> 11) & 3;
            c->info->tess_domain = c->tess_domain;
            break;
        case OP_DCL_HS_MAX_TESSFACTOR:
            c->max_tess_factor = ins.extra_count ? x[0] : 0;
            break;
        case OP_DCL_TESS_PARTITIONING:
            c->tess_partitioning = (ins.token >> 11) & 7;
            c->info->tess_partitioning = c->tess_partitioning;
            break;
        case OP_DCL_TESS_OUTPUT_PRIMITIVE:
            c->tess_primitive = (ins.token >> 11) & 7;
            c->info->tess_output_primitive = c->tess_primitive;
            break;
        case OP_DCL_INTERFACE:
        case OP_DCL_FUNCTION_BODY:
        case OP_DCL_FUNCTION_TABLE:
            return fail(c, "class linkage is not implemented");
        default:
            break;
        }
    }
    if (res < 0)
        return fail(c, "malformed bytecode");
    /* a hull shader without a control point phase passes its inputs through: the registers its outputs name. */
    if (c->program == DXBC_HULL && !c->has_cp_phase)
        for (uint32_t i = 0; i < c->info->outputs.count; i++) {
            uint32_t reg = c->info->outputs.elements[i].reg;
            if (reg >= MAX_REGS)
                continue;
            c->input_decl[reg].declared = 1;
            c->input_decl[reg].mask |= c->info->outputs.elements[i].mask;
            if (reg + 1 > c->input_count)
                c->input_count = reg + 1;
        }
    return 0;
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
    if (c->input_count) {
        uint32_t regs = spv_type_array(&c->b, v4, c->input_count);
        uint32_t vertices = c->program == DXBC_GEOMETRY ? c->gs_vertices
                            : c->program == DXBC_HULL || c->program == DXBC_DOMAIN ? (c->cp_in ? c->cp_in : 1)
                                                                                     : 0;
        c->inputs = global(c, SpvStorageClassPrivate, vertices ? spv_type_array(&c->b, regs, vertices) : regs, 0);
    }
    if (c->patch_count)
        c->patch_regs = global(c, SpvStorageClassPrivate, spv_type_array(&c->b, v4, c->patch_count), 0);
    if (c->program == DXBC_HULL)
        c->phase_instance = global(c, SpvStorageClassPrivate, c->t_u32, 0);
    if (c->output_count)
        c->outputs = global(c, SpvStorageClassPrivate, spv_type_array(&c->b, v4, c->output_count), 0);
    for (uint32_t i = 0; i < 256; i++)
        if (c->tgsm_size[i])
            c->tgsm[i] = global(c, SpvStorageClassWorkgroup, spv_type_array(&c->b, c->t_u32, c->tgsm_size[i]), 0);
    /* the stage's table, which direct3d 12 replaces with its root signature. */
    if (!c->options || !c->options->root_signature) {
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

/* ------------------------------------------------------------------ doubles */

/*
 * a register holds two doubles, x and z their low halves, y and w their
 * high ones. double operations work on both lanes of the swizzled sources
 * and write the lanes the destination mask names, xy the first and zw the
 * second.
 */
static uint32_t dvec2_type(Compiler *c)
{
    if (!c->t_dvec2) {
        spv_capability(&c->b, SpvCapabilityFloat64);
        c->t_f64 = spv_type_float(&c->b, 64);
        c->t_dvec2 = spv_type_vector(&c->b, c->t_f64, 2);
    }
    return c->t_dvec2;
}

static uint32_t dconst(Compiler *c, double value)
{
    union {
        double d;
        uint64_t u;
    } bits = {value};
    dvec2_type(c);
    uint32_t d = spv_const64(&c->b, c->t_f64, bits.u);
    uint32_t parts[2] = {d, d};
    return spv_const_composite(&c->b, c->t_dvec2, parts, 2);
}

/* the two doubles of a source, its modifiers applied to them. */
static uint32_t load_doubles(Compiler *c, const DxbcOperand *op)
{
    DxbcOperand plain = *op;
    plain.modifier = 0;
    uint32_t t = dvec2_type(c);
    uint32_t v = spv_op1(&c->b, SpvOpBitcast, t, load(c, &plain, 0xF, K_UINT));
    if (op->modifier & MOD_ABS)
        v = spv_glsl(&c->b, t, GLSLstd450FAbs, &v, 1);
    if (op->modifier & MOD_NEG)
        v = spv_op1(&c->b, SpvOpFNegate, t, v);
    return v;
}

/* the lanes of two doubles the destination mask names, saturated if asked. */
static void store_doubles(Compiler *c, const DxbcInstruction *ins, const DxbcOperand *dst, uint32_t value)
{
    if (ins->saturate) {
        uint32_t args[3] = {value, dconst(c, 0.0), dconst(c, 1.0)};
        value = spv_glsl(&c->b, c->t_dvec2, GLSLstd450NClamp, args, 3);
    }
    uint32_t bits = spv_op1(&c->b, SpvOpBitcast, c->t_vec[K_UINT][4], value);
    uint8_t mask = dst_mask(dst);
    uint32_t n = popcount(mask), picks[4], count = 0;
    for (uint32_t i = 0; i < 4; i++)
        if (mask & (1 << i))
            picks[count++] = i;
    if (n == 1)
        bits = extract(c, c->t_u32, bits, picks[0]);
    else if (n < 4) {
        uint32_t w[6] = {bits, bits, picks[0], picks[1], picks[2], picks[3]};
        bits = spv_op(&c->b, SpvOpVectorShuffle, vec_type(c, K_UINT, n), w, 2 + n);
    }
    store(c, NULL, dst, bits, K_UINT);
}

/* 32 bit results of the double lanes, to the destination's components in order. */
static void store_lanes(Compiler *c, const DxbcOperand *dst, uint32_t value, Kind k)
{
    uint32_t n = popcount(dst_mask(dst));
    if (n == 1)
        value = extract(c, vec_type(c, k, 1), value, 0);
    store(c, NULL, dst, value, k);
}

static void double_binary(Compiler *c, const DxbcInstruction *ins, uint32_t op)
{
    uint32_t a = load_doubles(c, &ins->operands[1]), b = load_doubles(c, &ins->operands[2]);
    uint32_t r = spv_op2(&c->b, op, c->t_dvec2, a, b);
    decorate_precise(c, ins, r);
    store_doubles(c, ins, &ins->operands[0], r);
}

static void double_glsl(Compiler *c, const DxbcInstruction *ins, uint32_t instruction, uint32_t args)
{
    uint32_t v[3];
    for (uint32_t i = 0; i < args; i++)
        v[i] = load_doubles(c, &ins->operands[1 + i]);
    uint32_t r = spv_glsl(&c->b, c->t_dvec2, instruction, v, args);
    decorate_precise(c, ins, r);
    store_doubles(c, ins, &ins->operands[0], r);
}

static void double_compare(Compiler *c, const DxbcInstruction *ins, uint32_t op)
{
    uint32_t a = load_doubles(c, &ins->operands[1]), b = load_doubles(c, &ins->operands[2]);
    uint32_t r = bool_to_mask(c, spv_op2(&c->b, op, c->t_bvec[2], a, b), 2);
    store_lanes(c, &ins->operands[0], r, K_UINT);
}

/* doubles to 32 bit values, to the destination's components in order. */
static void double_to(Compiler *c, const DxbcInstruction *ins, uint32_t op, Kind out)
{
    uint32_t a = load_doubles(c, &ins->operands[1]);
    uint32_t r = spv_op1(&c->b, op, vec_type(c, out, 2), a);
    store_lanes(c, &ins->operands[0], r, out);
}

/* 32 bit values to doubles: the source's first two components, lane by lane. */
static void double_from(Compiler *c, const DxbcInstruction *ins, uint32_t op, Kind in)
{
    uint32_t a = load(c, &ins->operands[1], 3, in);
    uint32_t r = spv_op1(&c->b, op, dvec2_type(c), a);
    store_doubles(c, ins, &ins->operands[0], r);
}

static void double_movc(Compiler *c, const DxbcInstruction *ins)
{
    uint32_t cond = nonzero(c, load(c, &ins->operands[1], 3, K_UINT), 2);
    uint32_t a = load_doubles(c, &ins->operands[2]), b = load_doubles(c, &ins->operands[3]);
    store_doubles(c, ins, &ins->operands[0], spv_op3(&c->b, SpvOpSelect, c->t_dvec2, cond, a, b));
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

/*
 * an image read, through its sparse twin for the forms that write a
 * residency status when reads may find tiles mapped to nothing: the
 * status is 0 where every texel was mapped and 1 where one was not.
 */
static uint32_t image_read(Compiler *c, uint32_t opcode, uint32_t type, const uint32_t *w, uint32_t count)
{
    uint32_t sparse = 0;
    if (c->feedback && c->options->residency) {
        switch (opcode) {
        case SpvOpImageSampleImplicitLod: sparse = SpvOpImageSparseSampleImplicitLod; break;
        case SpvOpImageSampleExplicitLod: sparse = SpvOpImageSparseSampleExplicitLod; break;
        case SpvOpImageSampleDrefImplicitLod: sparse = SpvOpImageSparseSampleDrefImplicitLod; break;
        case SpvOpImageSampleDrefExplicitLod: sparse = SpvOpImageSparseSampleDrefExplicitLod; break;
        case SpvOpImageFetch: sparse = SpvOpImageSparseFetch; break;
        case SpvOpImageGather: sparse = SpvOpImageSparseGather; break;
        case SpvOpImageDrefGather: sparse = SpvOpImageSparseDrefGather; break;
        case SpvOpImageRead: sparse = SpvOpImageSparseRead; break;
        default: break;
        }
    }
    if (!sparse)
        return spv_op(&c->b, opcode, type, w, count);
    spv_capability(&c->b, SpvCapabilitySparseResidency);
    uint32_t members[2] = {c->t_i32, type};
    uint32_t r = spv_op(&c->b, sparse, spv_type_struct(&c->b, members, 2), w, count);
    uint32_t resident = spv_op1(&c->b, SpvOpImageSparseTexelsResident, c->t_bool, extract(c, c->t_i32, r, 0));
    c->feedback_status = spv_op3(&c->b, SpvOpSelect, c->t_u32, resident, uconst(c, 0), uconst(c, 1));
    return extract(c, type, r, 1);
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
    uint32_t image = load_srv(c, resource, depth, &image_type);
    uint32_t si = spv_op2(&c->b, SpvOpSampledImage, spv_type_sampled_image(&c->b, image_type), image,
                          load_sampler(c, sampler));
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
    if (c->min_lod && (implicit || ins->opcode == OP_SAMPLE_D)) {
        spv_capability(&c->b, SpvCapabilityMinLod);
        operands |= SpvImageOperandsMinLodMask;
        w[count++] = load_scalar(c, c->min_lod, c->min_lod->swizzle[0], K_FLOAT);
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
        result = splat(c, K_FLOAT, 4, image_read(c, op, c->t_f32, w, count));
        store_texel(c, ins, resource, result, K_FLOAT);
        return;
    }
    uint32_t op = implicit ? SpvOpImageSampleImplicitLod : SpvOpImageSampleExplicitLod;
    result = image_read(c, op, vec_type(c, r->kind, 4), w, count);
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
    uint32_t image = load_srv(c, resource, cmp, &image_type);
    uint32_t si = spv_op2(&c->b, SpvOpSampledImage, spv_type_sampled_image(&c->b, image_type), image,
                          load_sampler(c, sampler));
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
    uint32_t result = image_read(c, cmp ? SpvOpImageDrefGather : SpvOpImageGather, vec_type(c, k, 4), w, count);
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
    uint32_t image = load_srv(c, resource, 0, &image_type);
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
    uint32_t result = image_read(c, SpvOpImageFetch, vec_type(c, r->kind, 4), w, count);
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
        uint32_t ptr = uav_pointer(c, resource, &image_type);
        image = spv_load(&c->b, image_type, ptr);
    } else {
        image = load_srv(c, resource, 0, &image_type);
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
    uint32_t size;
    RootBuffer rb;
    if (root_buffer(c, resource, &rb)) {
        /* a record has the size in bytes; root descriptors have none. */
        size = rb.size ? rb.size : uconst(c, 0);
    } else {
        spv_capability(&c->b, SpvCapabilityImageQuery);
        uint32_t image_type, image;
        if (resource->type == OPERAND_UNORDERED_ACCESS_VIEW) {
            uint32_t ptr = uav_pointer(c, resource, &image_type);
            image = spv_load(&c->b, image_type, ptr);
        } else {
            image = load_srv(c, resource, 0, &image_type);
        }
        size = spv_op1(&c->b, SpvOpImageQuerySize, c->t_u32, image);
        if (r->raw || r->structured)
            size = spv_op2(&c->b, SpvOpIMul, c->t_u32, size, uconst(c, 4));
    }
    if (r->structured && r->stride)
        size = spv_op2(&c->b, SpvOpUDiv, c->t_u32, size, uconst(c, r->stride));
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
    uint32_t image = load_srv(c, resource, 0, &image_type);
    uint32_t si = spv_op2(&c->b, SpvOpSampledImage, spv_type_sampled_image(&c->b, image_type), image,
                          load_sampler(c, sampler));
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
        /* the render targets' sample count, which the device puts in the table. */
        count = rasterizer_samples(c);
    } else {
        Resource *r;
        int slot = resource_slot(c, resource, &r);
        if (slot < 0)
            return;
        spv_capability(&c->b, SpvCapabilityImageQuery);
        uint32_t image_type;
        uint32_t image = load_srv(c, resource, 0, &image_type);
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
    RootBuffer rb;
    if (root_buffer(c, resource, &rb)) {
        uint32_t in = 0;
        if (rb.size) {
            in = word_in_bounds(c, &rb, word);
            word = spv_op3(&c->b, SpvOpSelect, c->t_u32, in, word, uconst(c, 0));
        }
        uint32_t w[3] = {root_word_pointer(c, rb.address, word), SpvMemoryAccessAlignedMask, 4};
        uint32_t v = spv_op(&c->b, SpvOpLoad, c->t_u32, w, 3);
        return in ? spv_op3(&c->b, SpvOpSelect, c->t_u32, in, v, uconst(c, 0)) : v;
    }
    uint32_t image_type, image;
    if (resource->type == OPERAND_UNORDERED_ACCESS_VIEW) {
        uint32_t ptr = uav_pointer(c, resource, &image_type);
        image = spv_load(&c->b, image_type, ptr);
        return extract(c, c->t_u32, spv_op2(&c->b, SpvOpImageRead, c->t_vec[K_UINT][4], image, word), 0);
    }
    image = load_srv(c, resource, 0, &image_type);
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
    RootBuffer rb;
    if (root_buffer(c, resource, &rb)) {
        uint32_t merge = 0;
        if (rb.size)
            begin_conditional(c, word_in_bounds(c, &rb, word), &merge);
        uint32_t w[4] = {root_word_pointer(c, rb.address, word), value, SpvMemoryAccessAlignedMask, 4};
        spv_op_void(&c->b, SpvOpStore, w, 4);
        if (merge) {
            spv_branch(&c->b, merge);
            spv_label(&c->b, merge);
        }
        return;
    }
    uint32_t image_type;
    uint32_t ptr = uav_pointer(c, resource, &image_type);
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
    uint32_t ptr = uav_pointer(c, resource, &image_type);
    uint32_t image = spv_load(&c->b, image_type, ptr);
    uint32_t coord = load_first(c, &ins->operands[1], coord_count(r->dim), K_UINT);
    uint32_t words[2] = {image, coord};
    uint32_t v = image_read(c, SpvOpImageRead, vec_type(c, r->kind, 4), words, 2);
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
    uint32_t ptr = uav_pointer(c, resource, &image_type);
    uint32_t image = spv_load(&c->b, image_type, ptr);
    uint32_t coord = load_first(c, &ins->operands[1], coord_count(r->dim), K_UINT);
    uint32_t value = load(c, &ins->operands[2], 0xF, r->kind);
    uint32_t w[3] = {image, coord, value};
    spv_op_void(&c->b, SpvOpImageWrite, w, 3);
}

/* a pointer to the word an atomic works on, in a view or in shared memory, and whether it is in bounds. */
static uint32_t atomic_pointer(Compiler *c, const DxbcOperand *resource, const DxbcOperand *address, Kind *k,
                               uint32_t *scope, uint32_t *in)
{
    *k = K_UINT;
    *in = 0;
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
    RootBuffer rb;
    if (root_buffer(c, resource, &rb)) {
        DxbcOperand offset = *address;
        offset.swizzle[0] = address->swizzle[1];
        *scope = SpvScopeDevice;
        uint32_t word = r->structured ? word_index(c, address, &offset, r->stride) : word_index(c, address, NULL, 0);
        if (rb.size) {
            *in = word_in_bounds(c, &rb, word);
            word = spv_op3(&c->b, SpvOpSelect, c->t_u32, *in, word, uconst(c, 0));
        }
        return root_word_pointer(c, rb.address, word);
    }
    uint32_t image_type;
    uint32_t image_ptr = uav_pointer(c, resource, &image_type);
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
    uint32_t scope, in;
    uint32_t ptr = atomic_pointer(c, resource, address, &k, &scope, &in);
    if (!ptr)
        return;
    uint32_t type = scalar_type(c, k);
    /* out of bounds atomics of direct3d 12's views do nothing and return zero. */
    uint32_t before = c->b.block, merge = 0;
    if (in)
        begin_conditional(c, in, &merge);
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
    if (merge) {
        uint32_t then = c->b.block;
        spv_branch(&c->b, merge);
        spv_label(&c->b, merge);
        uint32_t w[4] = {result, then, uconst(c, 0), before};
        result = spv_op(&c->b, SpvOpPhi, type, w, 4);
    }
    if (immediate)
        store(c, ins, &ins->operands[0], splat(c, k, popcount(dst_mask(&ins->operands[0])), result), k);
}

/* imm_atomic_alloc and consume: the view's hidden counter. */
static void counter(Compiler *c, const DxbcInstruction *ins)
{
    const DxbcOperand *view = &ins->operands[1];
    uint32_t slot = view->index[0].immediate & 63;
    uint32_t ptr;
    RootBuffer rb;
    if (root_buffer(c, view, &rb)) {
        /* direct3d 12's record of the view has its counter's address. */
        if (!rb.counter) {
            fail(c, "counter of a root descriptor");
            return;
        }
        ptr = root_word_pointer(c, rb.counter, uconst(c, 0));
    } else {
        uint32_t image_type = spv_type_image(&c->b, c->t_u32, SpvDimBuffer, 0, 0, 0, 2, SpvImageFormatR32ui);
        spv_capability(&c->b, SpvCapabilityImageBuffer);
        uint32_t image_ptr =
            descriptor_pointer(c, image_type, DXBC_HEAP_STORAGE_TEXEL, table_entry(c, DXBC_TABLE_UAV_COUNTER + slot));
        uint32_t w[3] = {image_ptr, uconst(c, 0), uconst(c, 0)};
        ptr = spv_op(&c->b, SpvOpImageTexelPointer, spv_type_pointer(&c->b, SpvStorageClassImage, c->t_u32), w, 3);
    }
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

/*
 * float to integer conversions as direct3d defines them where vulkan does
 * not: values past the range saturate, nan becomes 0.
 */
static void float_to_int(Compiler *c, const DxbcInstruction *ins, int is_signed)
{
    uint8_t mask = dst_mask(&ins->operands[0]);
    uint32_t n = popcount(mask);
    Kind k = is_signed ? K_INT : K_UINT;
    uint32_t t = vec_type(c, k, n), b = c->t_bvec[n];
    uint32_t x = load(c, &ins->operands[1], mask, K_FLOAT);
    /* the largest floats below 2^31 and 2^32. */
    uint32_t args[3] = {x, splat_const(c, K_FLOAT, n, is_signed ? 0xCF000000 : 0),
                        splat_const(c, K_FLOAT, n, is_signed ? 0x4EFFFFFF : 0x4F7FFFFF)};
    uint32_t clamped = spv_glsl(&c->b, vec_type(c, K_FLOAT, n), GLSLstd450NClamp, args, 3);
    uint32_t r = spv_op1(&c->b, is_signed ? SpvOpConvertFToS : SpvOpConvertFToU, t, clamped);
    uint32_t big = spv_op2(&c->b, SpvOpFOrdGreaterThanEqual, b, x,
                           splat_const(c, K_FLOAT, n, is_signed ? 0x4F000000 : 0x4F800000));
    r = spv_op3(&c->b, SpvOpSelect, t, big, splat_const(c, k, n, is_signed ? 0x7FFFFFFF : 0xFFFFFFFF), r);
    if (is_signed)
        r = spv_op3(&c->b, SpvOpSelect, t, spv_op1(&c->b, SpvOpIsNan, b, x), splat_const(c, K_INT, n, 0), r);
    store(c, ins, &ins->operands[0], r, k);
}

/* masked sums of absolute differences: the bytes of each component where the reference byte is not zero. */
static void msad(Compiler *c, const DxbcInstruction *ins)
{
    uint8_t mask = dst_mask(&ins->operands[0]);
    uint32_t n = popcount(mask), u = vec_type(c, K_UINT, n), i = vec_type(c, K_INT, n);
    uint32_t ref = load(c, &ins->operands[1], mask, K_UINT), src = load(c, &ins->operands[2], mask, K_UINT);
    uint32_t sum = load(c, &ins->operands[3], mask, K_UINT);
    for (uint32_t b = 0; b < 4; b++) {
        uint32_t at = uconst(c, 8 * b), eight = uconst(c, 8);
        uint32_t rb = spv_op3(&c->b, SpvOpBitFieldUExtract, u, ref, at, eight);
        uint32_t sb = spv_op3(&c->b, SpvOpBitFieldUExtract, u, src, at, eight);
        uint32_t d = spv_op2(&c->b, SpvOpISub, i, bitcast(c, rb, K_UINT, K_INT, n), bitcast(c, sb, K_UINT, K_INT, n));
        d = bitcast(c, spv_glsl(&c->b, i, GLSLstd450SAbs, &d, 1), K_INT, K_UINT, n);
        d = spv_op3(&c->b, SpvOpSelect, u, nonzero(c, rb, n), d, splat_const(c, K_UINT, n, 0));
        sum = spv_op2(&c->b, SpvOpIAdd, u, sum, d);
    }
    store(c, ins, &ins->operands[0], sum, K_UINT);
}

/*
 * direct3d's standard sample positions, in sixteenths of a pixel from its
 * center, for 1, 2, 4, 8 and 16 samples: those of count n start at n - 1.
 * vulkan's standard sample locations are the same.
 */
static const int8_t SAMPLE_POSITIONS[31][2] = {
    {0, 0},   {4, 4},   {-4, -4}, {-2, -6}, {6, -2}, {-6, 2}, {2, 6},  {1, -3},  {-1, 3},  {5, 1},   {-3, -5},
    {-5, 5},  {-7, -1}, {3, 7},   {7, -7},  {1, 1},  {-1, -3}, {-3, 2}, {4, -1},  {-5, -2}, {2, 5},   {5, 3},
    {3, -5},  {-2, 6},  {0, -7},  {-4, -6}, {-6, 4}, {-8, 0},  {7, -4}, {6, 7},   {-7, -8}};

/* the sample count of a resource operand, or of the render targets for the rasterizer. */
static uint32_t sample_count(Compiler *c, const DxbcOperand *resource)
{
    if (resource->type == OPERAND_RASTERIZER)
        return rasterizer_samples(c);
    Resource *r;
    int slot = resource_slot(c, resource, &r);
    if (slot < 0 || (r->dim != RES_TEXTURE2DMS && r->dim != RES_TEXTURE2DMSARRAY))
        return uconst(c, 1);
    spv_capability(&c->b, SpvCapabilityImageQuery);
    uint32_t image_type;
    uint32_t image = load_srv(c, resource, 0, &image_type);
    return spv_op1(&c->b, SpvOpImageQuerySamples, c->t_u32, image);
}

/* where a sample is in its pixel; the center for a sample index past the count, or counts without a pattern. */
static void samplepos(Compiler *c, const DxbcInstruction *ins)
{
    const DxbcOperand *resource = &ins->operands[1];
    uint32_t count = sample_count(c, resource);
    uint32_t sample = load_scalar(c, &ins->operands[2], ins->operands[2].swizzle[0], K_UINT);
    if (!c->sample_positions) {
        uint32_t v2 = c->t_vec[K_FLOAT][2], parts[31];
        for (uint32_t i = 0; i < 31; i++) {
            uint32_t xy[2] = {fconst(c, SAMPLE_POSITIONS[i][0] / 16.0f), fconst(c, SAMPLE_POSITIONS[i][1] / 16.0f)};
            parts[i] = spv_const_composite(&c->b, v2, xy, 2);
        }
        uint32_t array = spv_type_array(&c->b, v2, 31);
        c->sample_positions =
            global(c, SpvStorageClassPrivate, array, spv_const_composite_array(&c->b, array, parts, 31));
    }
    uint32_t one = uconst(c, 1);
    uint32_t pow2 = spv_op2(&c->b, SpvOpIEqual, c->t_bool,
                            spv_op2(&c->b, SpvOpBitwiseAnd, c->t_u32, count, spv_op2(&c->b, SpvOpISub, c->t_u32, count, one)),
                            uconst(c, 0));
    uint32_t valid = spv_op2(&c->b, SpvOpLogicalAnd, c->t_bool, pow2,
                             spv_op2(&c->b, SpvOpLogicalAnd, c->t_bool,
                                     spv_op2(&c->b, SpvOpULessThan, c->t_bool, sample, count),
                                     spv_op2(&c->b, SpvOpULessThanEqual, c->t_bool, count, uconst(c, 16))));
    uint32_t index = spv_op3(&c->b, SpvOpSelect, c->t_u32, valid,
                             spv_op2(&c->b, SpvOpIAdd, c->t_u32, spv_op2(&c->b, SpvOpISub, c->t_u32, count, one), sample),
                             uconst(c, 0));
    uint32_t ptr = spv_access(&c->b, spv_type_pointer(&c->b, SpvStorageClassPrivate, c->t_vec[K_FLOAT][2]),
                              c->sample_positions, &index, 1);
    uint32_t xy = spv_load(&c->b, c->t_vec[K_FLOAT][2], ptr);
    uint32_t zero = fconst(c, 0.0f);
    uint32_t parts[4] = {extract(c, c->t_f32, xy, 0), extract(c, c->t_f32, xy, 1), zero, zero};
    store_texel(c, ins, resource, spv_op(&c->b, SpvOpCompositeConstruct, c->t_vec[K_FLOAT][4], parts, 4), K_FLOAT);
}

/*
 * the forms of tiled resources that also write a residency status: the
 * plain form, its reads sparse ones where tiles may map nothing, and their
 * status, which check_access_fully_mapped reads, 0 when all was mapped.
 * the clamped forms clamp the level of detail of the sample.
 */
static int instruction(Compiler *c, const DxbcInstruction *ins, uint32_t at);

static int feedback(Compiler *c, const DxbcInstruction *ins, uint32_t at)
{
    static const uint8_t PLAIN[] = {OP_GATHER4,    OP_GATHER4_C,  OP_GATHER4_PO,      OP_GATHER4_PO_C, OP_LD,
                                    OP_LD_MS,      OP_LD_UAV_TYPED, OP_LD_RAW,        OP_LD_STRUCTURED, OP_SAMPLE_L,
                                    OP_SAMPLE_C_LZ, OP_SAMPLE,     OP_SAMPLE_B,       OP_SAMPLE_D,     OP_SAMPLE_C};
    DxbcInstruction plain = *ins;
    plain.opcode = PLAIN[ins->opcode - OP_GATHER4_FEEDBACK];
    if (plain.operand_count < 3)
        return fail(c, "malformed bytecode");
    for (uint32_t i = 1; i + 1 < plain.operand_count; i++)
        plain.operands[i] = plain.operands[i + 1];
    plain.operand_count--;
    int clamped = ins->opcode >= OP_SAMPLE_CLAMP_FEEDBACK;
    if (clamped)
        c->min_lod = &ins->operands[--plain.operand_count + 1];
    c->feedback = 1;
    c->feedback_status = 0;
    int result = instruction(c, &plain, at);
    c->feedback = 0;
    c->min_lod = NULL;
    const DxbcOperand *status = &ins->operands[1];
    uint32_t n = popcount(dst_mask(status));
    store(c, NULL, status, c->feedback_status ? splat(c, K_UINT, n, c->feedback_status) : splat_const(c, K_UINT, n, 0),
          K_UINT);
    return result;
}

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
    case OP_DMOV:
        store_doubles(c, ins, &ins->operands[0], load_doubles(c, &ins->operands[1]));
        break;
    case OP_MSAD:
        msad(c, ins);
        break;
    case OP_SAMPLE_POS:
        samplepos(c, ins);
        break;
    case OP_GATHER4_FEEDBACK:
    case OP_GATHER4_C_FEEDBACK:
    case OP_GATHER4_PO_FEEDBACK:
    case OP_GATHER4_PO_C_FEEDBACK:
    case OP_LD_FEEDBACK:
    case OP_LD_MS_FEEDBACK:
    case OP_LD_UAV_TYPED_FEEDBACK:
    case OP_LD_RAW_FEEDBACK:
    case OP_LD_STRUCTURED_FEEDBACK:
    case OP_SAMPLE_L_FEEDBACK:
    case OP_SAMPLE_C_LZ_FEEDBACK:
    case OP_SAMPLE_CLAMP_FEEDBACK:
    case OP_SAMPLE_B_CLAMP_FEEDBACK:
    case OP_SAMPLE_D_CLAMP_FEEDBACK:
    case OP_SAMPLE_C_CLAMP_FEEDBACK:
        return feedback(c, ins, at);
    case OP_CHECK_ACCESS_FULLY_MAPPED: {
        uint32_t status = load_scalar(c, &ins->operands[1], ins->operands[1].swizzle[0], K_UINT);
        uint32_t mapped = spv_op3(&c->b, SpvOpSelect, c->t_u32,
                                  spv_op2(&c->b, SpvOpIEqual, c->t_bool, status, uconst(c, 0)), uconst(c, ~0u),
                                  uconst(c, 0));
        store(c, NULL, &ins->operands[0], splat(c, K_UINT, popcount(dst_mask(&ins->operands[0])), mapped), K_UINT);
        break;
    }
    case OP_ABORT:
    case OP_DEBUG_BREAK:
        break;
    case OP_INTERFACE_CALL:
        return fail(c, "class linkage is not implemented");
    case OP_DADD:
        double_binary(c, ins, SpvOpFAdd);
        break;
    case OP_DMUL:
        double_binary(c, ins, SpvOpFMul);
        break;
    case OP_DDIV:
        double_binary(c, ins, SpvOpFDiv);
        break;
    case OP_DMIN:
        double_glsl(c, ins, GLSLstd450NMin, 2);
        break;
    case OP_DMAX:
        double_glsl(c, ins, GLSLstd450NMax, 2);
        break;
    case OP_DFMA:
        double_glsl(c, ins, GLSLstd450Fma, 3);
        break;
    case OP_DRCP: {
        uint32_t r = spv_op2(&c->b, SpvOpFDiv, dvec2_type(c), dconst(c, 1.0), load_doubles(c, &ins->operands[1]));
        store_doubles(c, ins, &ins->operands[0], r);
        break;
    }
    case OP_DEQ:
        double_compare(c, ins, SpvOpFOrdEqual);
        break;
    case OP_DNE:
        double_compare(c, ins, SpvOpFUnordNotEqual);
        break;
    case OP_DLT:
        double_compare(c, ins, SpvOpFOrdLessThan);
        break;
    case OP_DGE:
        double_compare(c, ins, SpvOpFOrdGreaterThanEqual);
        break;
    case OP_DMOVC:
        double_movc(c, ins);
        break;
    case OP_DTOF:
        double_to(c, ins, SpvOpFConvert, K_FLOAT);
        break;
    case OP_DTOI:
        double_to(c, ins, SpvOpConvertFToS, K_INT);
        break;
    case OP_DTOU:
        double_to(c, ins, SpvOpConvertFToU, K_UINT);
        break;
    case OP_FTOD:
        double_from(c, ins, SpvOpFConvert, K_FLOAT);
        break;
    case OP_ITOD:
        double_from(c, ins, SpvOpConvertSToF, K_INT);
        break;
    case OP_UTOD:
        double_from(c, ins, SpvOpConvertUToF, K_UINT);
        break;
    case OP_MOV: {
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
    case OP_FTOU:
        float_to_int(c, ins, ins->opcode == OP_FTOI);
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
    case OP_EMIT:
    case OP_EMITTHENCUT:
    case OP_EMIT_STREAM:
    case OP_EMITTHENCUT_STREAM:
    case OP_CUT:
    case OP_CUT_STREAM: {
        int stream = ins->opcode == OP_EMIT_STREAM || ins->opcode == OP_EMITTHENCUT_STREAM ||
                     ins->opcode == OP_CUT_STREAM;
        uint32_t index = stream ? ins->operands[0].index[0].immediate : 0;
        int emit = ins->opcode != OP_CUT && ins->opcode != OP_CUT_STREAM;
        int cut = ins->opcode != OP_EMIT && ins->opcode != OP_EMIT_STREAM;
        if (index)
            spv_capability(&c->b, SpvCapabilityGeometryStreams);
        uint32_t s = uconst(c, index);
        if (emit) {
            if (index == c->raster_stream)
                store_outputs(c);
            store_captures(c, index);
            if (index)
                spv_op_void(&c->b, SpvOpEmitStreamVertex, &s, 1);
            else
                spv_op_void(&c->b, SpvOpEmitVertex, NULL, 0);
        }
        if (cut) {
            if (index)
                spv_op_void(&c->b, SpvOpEndStreamPrimitive, &s, 1);
            else
                spv_op_void(&c->b, SpvOpEndPrimitive, NULL, 0);
        }
        break;
    }
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

    /* the body, or a hull shader's phases, then the subroutines. */
    uint32_t void_fn = spv_type_function(b, c->t_void, NULL, 0);
    c->body = spv_id(b);
    if (c->program == DXBC_HULL) {
        make_control_point_outputs(c);
        if (c->has_cp_phase) {
            c->phase = PHASE_CONTROL_POINT;
            spv_function_begin(b, c->body, c->t_void, void_fn);
            spv_label(b, spv_id(b));
            if (compile_range(c, c->cp_start, c->cp_end))
                return -1;
            spv_function_end(b);
        }
        for (uint32_t i = 0; i < c->phase_count; i++) {
            c->phase = PHASE_FORK;
            spv_function_begin(b, c->phases[i].fn, c->t_void, void_fn);
            spv_label(b, spv_id(b));
            if (compile_range(c, c->phases[i].start, c->phases[i].end))
                return -1;
            spv_function_end(b);
        }
        c->phase = PHASE_NONE;
    } else {
        spv_function_begin(b, c->body, c->t_void, void_fn);
        spv_label(b, spv_id(b));
        uint32_t end = c->label_count ? c->label_at[0] : c->code_count;
        if (compile_range(c, 0, end))
            return -1;
        spv_function_end(b);
    }
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
    if (c->inputs) {
        if (c->program == DXBC_GEOMETRY)
            load_vertex_inputs(c, c->gs_vertices);
        else if (c->program == DXBC_HULL || c->program == DXBC_DOMAIN)
            load_vertex_inputs(c, c->cp_in ? c->cp_in : 1);
        else
            load_inputs(c);
    }
    if (c->program == DXBC_DOMAIN && c->patch_regs)
        load_patch_constants(c);
    if (c->program == DXBC_HULL) {
        hull_main(c, c->body);
    } else {
        spv_op(b, SpvOpFunctionCall, c->t_void, &c->body, 1);
        /* geometry shaders write their outputs at each emit. */
        if (c->program != DXBC_GEOMETRY)
            store_outputs(c);
    }
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
    } else if (c->program == DXBC_GEOMETRY) {
        spv_capability(b, SpvCapabilityGeometry);
        static const uint32_t INPUTS[8] = {0, SpvExecutionModeInputPoints, SpvExecutionModeInputLines,
                                           SpvExecutionModeTriangles, 0, 0, SpvExecutionModeInputLinesAdjacency,
                                           SpvExecutionModeInputTrianglesAdjacency};
        uint32_t output = c->gs_topology == 1 ? SpvExecutionModeOutputPoints
                          : c->gs_topology == 3 ? SpvExecutionModeOutputLineStrip
                                                : SpvExecutionModeOutputTriangleStrip;
        uint32_t modes[4][3] = {{main, INPUTS[c->gs_primitive & 7]},
                                {main, output},
                                {main, SpvExecutionModeOutputVertices, c->gs_max_vertices ? c->gs_max_vertices : 1},
                                {main, SpvExecutionModeInvocations, c->gs_instances ? c->gs_instances : 1}};
        for (int i = 0; i < 4; i++)
            spv_emit(b, SECTION_EXECUTION_MODES, SpvOpExecutionMode, modes[i], i < 2 ? 2 : 3);
        if (c->capture_count) {
            uint32_t x[2] = {main, SpvExecutionModeXfb};
            spv_emit(b, SECTION_EXECUTION_MODES, SpvOpExecutionMode, x, 2);
        }
    } else if (c->program == DXBC_HULL || c->program == DXBC_DOMAIN) {
        spv_capability(b, SpvCapabilityTessellation);
        /* the domain, then for hull shaders the partitioning and the primitives' winding or points. */
        static const uint32_t DOMAINS[4] = {SpvExecutionModeTriangles, SpvExecutionModeIsolines,
                                            SpvExecutionModeTriangles, SpvExecutionModeQuads};
        uint32_t domain[2] = {main, DOMAINS[c->tess_domain & 3]};
        spv_emit(b, SECTION_EXECUTION_MODES, SpvOpExecutionMode, domain, 2);
        if (c->program == DXBC_HULL) {
            uint32_t vertices[3] = {main, SpvExecutionModeOutputVertices, c->cp_out ? c->cp_out : 1};
            spv_emit(b, SECTION_EXECUTION_MODES, SpvOpExecutionMode, vertices, 3);
            static const uint32_t SPACING[5] = {SpvExecutionModeSpacingEqual, SpvExecutionModeSpacingEqual,
                                                SpvExecutionModeSpacingEqual, SpvExecutionModeSpacingFractionalOdd,
                                                SpvExecutionModeSpacingFractionalEven};
            uint32_t spacing[2] = {main, SPACING[c->tess_partitioning < 5 ? c->tess_partitioning : 0]};
            spv_emit(b, SECTION_EXECUTION_MODES, SpvOpExecutionMode, spacing, 2);
            uint32_t order = c->tess_primitive == 1   ? SpvExecutionModePointMode
                             : c->tess_primitive == 4 ? SpvExecutionModeVertexOrderCcw
                                                      : SpvExecutionModeVertexOrderCw;
            uint32_t primitive[2] = {main, order};
            if (c->tess_primitive != 2)
                spv_emit(b, SECTION_EXECUTION_MODES, SpvOpExecutionMode, primitive, 2);
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
    c->sm51 = info->major > 5 || (info->major == 5 && info->minor >= 1);
    /* with no stream rasterized the device discards what is; the first stream keeps the interface whole. */
    if (options && options->so_count && options->rasterized_stream < 4)
        c->raster_stream = options->rasterized_stream;
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
    mime_free(alloc, c->captures);
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
