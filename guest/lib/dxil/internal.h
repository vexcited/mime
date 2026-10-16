/* the compiler from dxil to spir-v, internal to it. */
#ifndef MIME_LIB_DXIL_INTERNAL_H
#define MIME_LIB_DXIL_INTERNAL_H

#include "cfg.h"
#include "dxil.h"
#include "ir.h"
#include "../dxbc/spvbuild.h"

#include "GLSL.std.450.h"

/*
 * dxil's enums, in the order DXIL.rst lists them; the numbers were checked
 * against the metadata and annotations dxc writes.
 */
enum {
    KIND_PIXEL,
    KIND_VERTEX,
    KIND_GEOMETRY,
    KIND_HULL,
    KIND_DOMAIN,
    KIND_COMPUTE,
    KIND_LIBRARY,
    KIND_RAYGEN,
    KIND_INTERSECTION,
    KIND_ANYHIT,
    KIND_CLOSESTHIT,
    KIND_MISS,
    KIND_CALLABLE,
    KIND_MESH = 13,
    KIND_AMPLIFICATION = 14,
};

enum {
    COMP_INVALID,
    COMP_I1,
    COMP_I16,
    COMP_U16,
    COMP_I32,
    COMP_U32,
    COMP_I64,
    COMP_U64,
    COMP_F16,
    COMP_F32,
    COMP_F64,
    COMP_SNORM_F16,
    COMP_UNORM_F16,
    COMP_SNORM_F32,
    COMP_UNORM_F32,
    COMP_SNORM_F64,
    COMP_UNORM_F64,
};

enum {
    SEM_ARBITRARY,
    SEM_VERTEX_ID,
    SEM_INSTANCE_ID,
    SEM_POSITION,
    SEM_RENDER_TARGET_ARRAY_INDEX,
    SEM_VIEWPORT_ARRAY_INDEX,
    SEM_CLIP_DISTANCE,
    SEM_CULL_DISTANCE,
    SEM_OUTPUT_CONTROL_POINT_ID,
    SEM_DOMAIN_LOCATION,
    SEM_PRIMITIVE_ID,
    SEM_GS_INSTANCE_ID,
    SEM_SAMPLE_INDEX,
    SEM_IS_FRONT_FACE,
    SEM_COVERAGE,
    SEM_INNER_COVERAGE,
    SEM_TARGET,
    SEM_DEPTH,
    SEM_DEPTH_LESS_EQUAL,
    SEM_DEPTH_GREATER_EQUAL,
    SEM_STENCIL_REF,
    SEM_DISPATCH_THREAD_ID,
    SEM_GROUP_ID,
    SEM_GROUP_INDEX,
    SEM_GROUP_THREAD_ID,
    SEM_TESS_FACTOR,
    SEM_INSIDE_TESS_FACTOR,
    SEM_VIEW_ID,
    SEM_BARYCENTRICS,
    SEM_SHADING_RATE,
    SEM_CULL_PRIMITIVE,
};

enum {
    INTERP_UNDEFINED,
    INTERP_CONSTANT,
    INTERP_LINEAR,
    INTERP_LINEAR_CENTROID,
    INTERP_NOPERSPECTIVE,
    INTERP_NOPERSPECTIVE_CENTROID,
    INTERP_LINEAR_SAMPLE,
    INTERP_NOPERSPECTIVE_SAMPLE,
};

enum {
    RK_INVALID,
    RK_TEXTURE1D,
    RK_TEXTURE2D,
    RK_TEXTURE2DMS,
    RK_TEXTURE3D,
    RK_TEXTURECUBE,
    RK_TEXTURE1DARRAY,
    RK_TEXTURE2DARRAY,
    RK_TEXTURE2DMSARRAY,
    RK_TEXTURECUBEARRAY,
    RK_TYPED_BUFFER,
    RK_RAW_BUFFER,
    RK_STRUCTURED_BUFFER,
    RK_CBUFFER,
    RK_SAMPLER,
    RK_TBUFFER,
    RK_ACCELERATION_STRUCTURE,
    RK_FEEDBACK_TEXTURE2D,
    RK_FEEDBACK_TEXTURE2DARRAY,
};

enum { CLASS_SRV, CLASS_UAV, CLASS_CBV, CLASS_SAMPLER };

/* a signature element and the variable or built-in it lives in. */
typedef struct SigElement {
    uint32_t id;
    const char *name;
    uint32_t semantic_index;
    uint8_t comp, kind, interp;
    uint32_t rows, cols;
    /* the packed place, -1 for system values outside the packed registers. */
    int32_t row;
    uint32_t col;
    /* made at the first access: the variable, the type of one row and of the whole. */
    uint32_t var;
    uint32_t row_type, var_type;
    uint32_t scalar;
    /* whether a built-in holds it, and how its components are indexed: 0 a scalar, 1 a vector, 2 by row, 3 distances. */
    uint8_t builtin;
    uint8_t shape;
    /* a value the rasterizer takes also goes to its location, for a following stage. */
    uint32_t loc_var, loc_scalar;
    /* a mesh shader's output no pixel shader reads. */
    uint8_t dropped;
    /* a pixel shader's input GetAttributeAtVertex reads, an array of the triangle's vertices. */
    uint8_t per_vertex;
    /* clip and cull distances: the first index in their built-in array. */
    uint32_t distance_base;
} SigElement;

typedef struct Signature {
    SigElement *elements;
    uint32_t count;
} Signature;

/* what a handle reaches: a range of the metadata or what annotateHandle says. */
typedef struct ResourceInfo {
    uint8_t cls, kind, comp, components;
    uint32_t space, lower, size;
    /* structured buffers: the stride; constant buffers: their size. */
    uint32_t stride;
    uint32_t samples;
    uint8_t counter, coherent, rov, comparison;
    /* libraries: the global standing for the range, 0 for none. */
    uint32_t global;
} ResourceInfo;

typedef struct Handle {
    ResourceInfo r;
    /* the dxil value of the register, absolute in its space, or of the heap index. */
    uint32_t index;
    uint8_t heap, nonuniform, annotated;
    /* libraries: the register as a spir-v value, instead of index. */
    uint32_t spv_register;
} Handle;

enum { VAL_NONE, VAL_PLAIN, VAL_PARTS, VAL_HANDLE, VAL_POINTER };

/* a dxil value as spir-v has it. */
typedef struct Value {
    uint8_t kind;
    /* the value stays in a variable, as its definition no longer dominates its uses. */
    uint8_t demoted;
    uint32_t id;
    /* the spir-v type of plain values, the pointee's of pointers. */
    uint32_t type;
    /* the storage class of pointers. */
    uint32_t storage;
    /* the components of a structure some operations return. */
    uint32_t *parts;
    uint32_t part_count;
    uint32_t handle;
    uint32_t var;
} Value;

/* an operand of a phi written before its value is known. */
typedef struct PhiFixup {
    size_t word;
    uint32_t block, phi, pred;
} PhiFixup;

typedef struct ImageVar {
    uint32_t type, var;
} ImageVar;

typedef struct Compiler {
    SpvBuilder b;
    const MimeAllocator *alloc;
    DxbcShaderInfo *info;
    const DxbcCompileOptions *options;
    BcArena arena;
    DxModule m;
    const DxFunction *f;
    Cfg *cfg;
    uint32_t kind;
    int native16;
    const char *error;

    /* types. */
    uint32_t t_void, t_bool, t_f32, t_u32, t_f16, t_u16, t_f64, t_u64;
    uint32_t t_vec[5], t_uvec[5], t_bvec[5];
    uint32_t *type_map;

    /* values: the module's, then the function's. */
    Value *module_values;
    Value *values;
    uint32_t value_count;
    Handle *handles;
    uint32_t handle_count, handle_capacity;
    /* the ids of the cfg's added values. */
    uint32_t *synth;
    /* by function value: the parts of a structure extractvalue reads, all ones when used whole. */
    uint32_t *part_masks;

    /* the entry point's description. */
    Signature inputs, outputs, patch;
    ResourceInfo *ranges[4];
    uint32_t range_count[4];
    uint32_t thread_group[3];
    uint32_t gs_input, gs_max_vertices, gs_topology, gs_instances, gs_streams;
    uint32_t cp_in, cp_out, tess_domain, tess_partitioning, tess_primitive;
    uint32_t max_tess_factor;
    /* mesh shaders: their outputs' counts and topology; task shaders: what the returns emit. */
    uint32_t mesh_vertices, mesh_primitives, mesh_topology;
    uint32_t payload, payload_type;
    uint32_t task_counts[3];
    const DxFunction *patch_function;
    uint64_t flags;
    int depth_mode;

    /* the interface the entry point lists. */
    uint32_t *interface;
    uint32_t interface_count, interface_capacity;
    struct {
        uint32_t which, storage, var;
    } builtins[64];
    uint32_t builtin_count;

    /* the root signature's objects. */
    uint32_t push;
    uint32_t static_samplers;
    uint32_t sampler_heap;
    uint32_t records;
    uint32_t root_vec4_pointer, root_word_pointer, root_qword_pointer;
    uint32_t sample_positions;
    ImageVar images[64];
    uint32_t image_count;
    ImageVar wide_images[8];
    uint32_t wide_image_count;

    /* the function being written. */
    uint32_t *labels;
    uint32_t *block_end;
    PhiFixup *fixups;
    uint32_t fixup_count, fixup_capacity;
    /* the resolved operands of phis, by fixup. */
    uint32_t *phi_values;
    uint32_t current;
    /* ray tracing stages: the variables of payloads and hit attributes, the shader record. */
    uint32_t hit_attributes, incoming, record;
    struct {
        uint32_t type, var, storage;
    } payloads[8];
    uint32_t payload_count;
    /* ray queries: an array of them, a handle its index, and the flags each allocation gives. */
    uint32_t ray_queries, ray_query_count, ray_query_flags;
    uint32_t *ray_query_constant_flags;
    /* by query, the instruction allocating it. */
    uint32_t *ray_query_insts;

    /* hull shaders: the invocation's control point, as dxil's outputs need it. */
    uint32_t invocation;
    int in_patch_function;
} Compiler;

int dxil_fail(Compiler *c, const char *error);
void *dxil_alloc(Compiler *c, size_t size);

uint32_t dxil_uconst(Compiler *c, uint32_t v);
uint32_t dxil_fconst(Compiler *c, float v);
uint32_t dxil_type(Compiler *c, uint32_t type);
/* the spir-v id of a plain value. */
uint32_t dxil_value(Compiler *c, uint32_t id);
Value *dxil_slot(Compiler *c, uint32_t id);
/* a constant integer operand, or fallback when it is not constant. */
uint64_t dxil_constant(Compiler *c, uint32_t id, uint64_t fallback);
int dxil_is_undef(Compiler *c, uint32_t id);
/* a value converted to a type: a bitcast or a conversion of widths. */
uint32_t dxil_convert(Compiler *c, uint32_t value, uint32_t from, uint32_t to);
uint32_t dxil_extract(Compiler *c, uint32_t type, uint32_t value, uint32_t index);
uint32_t dxil_vector(Compiler *c, uint32_t scalar_type, const uint32_t *parts, uint32_t count);
uint32_t dxil_component_type(Compiler *c, uint32_t type);
uint32_t dxil_global(Compiler *c, uint32_t storage, uint32_t type, uint32_t initializer);
void dxil_decorate(Compiler *c, uint32_t id, uint32_t decoration, uint32_t value);
uint32_t dxil_builtin(Compiler *c, uint32_t which, uint32_t storage, uint32_t type);
/* a structure result of several parts. */
void dxil_set_parts(Compiler *c, uint32_t id, const uint32_t *parts, uint32_t count);
void dxil_set_plain(Compiler *c, uint32_t id, uint32_t spv);
/* the start and end of an if, inside the block being written. */
void dxil_begin_if(Compiler *c, uint32_t cond, uint32_t *merge);
void dxil_end_if(Compiler *c, uint32_t merge);

void dxil_set_pointer(Compiler *c, uint32_t id, uint32_t ptr, uint32_t type, uint32_t storage);
/* the value a pointer operand points to, and its type. */
uint32_t dxil_load_pointer(Compiler *c, uint32_t id, uint32_t *type);
void dxil_store_pointer(Compiler *c, uint32_t id, uint32_t value);

/* io.c: signatures and the stages' own operations. */
int dxil_read_signatures(Compiler *c, const DxMetadata *list);
int dxil_io_op(Compiler *c, const DxInst *ins, uint32_t op);
void dxil_execution_modes(Compiler *c, uint32_t main);
/* task shaders: the payload and counts their returns emit, found before the code is written. */
int dxil_prepare_task(Compiler *c, const DxFunction *f);
int dxil_prepare_vertex_inputs(Compiler *c, const DxFunction *f);
void dxil_emit_tasks(Compiler *c);

/* resource.c: handles and the operations of resources. */
int dxil_read_resources(Compiler *c, const DxMetadata *list);
/* libraries: a load or element of a resource's global, which gives a handle; 1 when it is one. */
int dxil_resource_global(Compiler *c, const DxInst *ins);
int dxil_resource_op(Compiler *c, const DxInst *ins, uint32_t op);

/* ops.c: arithmetic, waves and the rest of dx.op. */
int dxil_math_op(Compiler *c, const DxInst *ins, uint32_t op);

/* raytracing.c: ray queries and the ray tracing stages. */
void dxil_prepare_ray_queries(Compiler *c, const DxFunction *f);
int dxil_ray_op(Compiler *c, const DxInst *ins, uint32_t op);
int dxil_is_ray_stage(uint32_t kind);
/* an entry function's payload and attributes, the variables of the stage. */
int dxil_ray_arguments(Compiler *c, const DxFunction *f);
/* the acceleration structure a handle reaches. */
uint32_t dxil_acceleration_structure(Compiler *c, uint32_t handle);

#endif
