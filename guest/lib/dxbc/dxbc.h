/*
 * dxbc: direct3d 10 and 11 shader bytecode, read from its container and
 * compiled to spir-v for vulkan. freestanding, so the guest and the host
 * build the same code.
 *
 * mime's binding model: descriptor set 0 is the heap, one array per kind of
 * descriptor, and every view and sampler has an index in it. set 1 holds a
 * uniform buffer per stage at binding = stage, a table the device fills with
 * the heap index of what is bound to each slot. a shader's table is compact:
 * it holds only the entries the shader reads, in table_entries order.
 */
#ifndef MIME_LIB_DXBC_H
#define MIME_LIB_DXBC_H

#include <stddef.h>
#include <stdint.h>

#include "../alloc.h"

#define DXBC_FOURCC(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)

/* heap bindings in set 0. */
enum {
    DXBC_HEAP_SAMPLED_IMAGE,
    DXBC_HEAP_STORAGE_IMAGE,
    DXBC_HEAP_UNIFORM_TEXEL,
    DXBC_HEAP_STORAGE_TEXEL,
    DXBC_HEAP_SAMPLER,
    DXBC_HEAP_UNIFORM_BUFFER,
    DXBC_HEAP_BINDINGS
};

#define DXBC_SET_HEAP 0
#define DXBC_SET_TABLE 1

/* the full table layout: where each kind of slot starts, in dwords. */
#define DXBC_TABLE_CB 0
#define DXBC_TABLE_SAMPLER 14
#define DXBC_TABLE_SRV 30
#define DXBC_TABLE_UAV 158
/* append and consume view counters, as storage texel buffers. */
#define DXBC_TABLE_UAV_COUNTER 222
/* not a heap index: the render target sample count, for the rasterizer operand. */
#define DXBC_TABLE_SAMPLE_COUNT 288
#define DXBC_TABLE_DWORDS 292

typedef enum DxbcProgram {
    DXBC_PIXEL = 0,
    DXBC_VERTEX = 1,
    DXBC_GEOMETRY = 2,
    DXBC_HULL = 3,
    DXBC_DOMAIN = 4,
    DXBC_COMPUTE = 5,
} DxbcProgram;

/* an element of an ISGN, OSGN or PCSG chunk or one of their variants. */
typedef struct DxbcSignatureElement {
    const char *name;
    uint32_t semantic_index;
    uint32_t system_value;
    uint32_t component_type;
    uint32_t reg;
    uint8_t mask;
    uint8_t used_mask;
    uint32_t stream;
    uint32_t min_precision;
} DxbcSignatureElement;

typedef struct DxbcSignature {
    uint32_t count;
    DxbcSignatureElement *elements;
} DxbcSignature;

/* a stream output declaration, as CreateGeometryShaderWithStreamOutput takes it. */
typedef struct DxbcStreamOutput {
    uint32_t stream;
    const char *semantic_name;
    uint32_t semantic_index;
    uint8_t start_component;
    uint8_t component_count;
    uint8_t output_slot;
} DxbcStreamOutput;

/*
 * direct3d 12: where a space's registers come from, per the root signature.
 * constants and root descriptors live in the push constants at push_offset,
 * as the values themselves or a buffer's device address. a table is the
 * heap index of its first descriptor there, and offset is the place of
 * register base in the table. a static sampler is element offset of the
 * static sampler set.
 */
enum { DXBC_BIND_CBV, DXBC_BIND_SRV, DXBC_BIND_UAV, DXBC_BIND_SAMPLER };
enum { DXBC_SOURCE_CONSTANTS, DXBC_SOURCE_ADDRESS, DXBC_SOURCE_TABLE, DXBC_SOURCE_STATIC_SAMPLER };

typedef struct DxbcRootBinding {
    uint8_t type;
    uint8_t source;
    /* local root signature: push_offset is in the shader record, and tables are descriptor handles. */
    uint8_t record;
    uint32_t space, base, count;
    uint32_t push_offset;
    uint32_t offset;
} DxbcRootBinding;

/*
 * direct3d 12 sets, each at binding 0: the resource heap's mutable
 * descriptors, the sampler heap, the static samplers and the resource
 * heap's records. a record describes the buffer behind a descriptor for
 * views read through device addresses: constant buffers, raw and
 * structured views.
 */
#define DXBC_ROOT_SET_RESOURCES 0
#define DXBC_ROOT_SET_SAMPLERS 1
#define DXBC_ROOT_SET_STATIC_SAMPLERS 2
#define DXBC_ROOT_SET_RECORDS 3
/* 64-bit atomics on typed views use 64-bit texel views beside the heap's own, at the same index. */
#define DXBC_ROOT_SET_ATOMICS 4

typedef struct DxbcRecord {
    uint64_t address;
    uint32_t size;
    uint32_t stride;
    uint64_t counter;
    uint64_t reserved;
} DxbcRecord;

typedef struct DxbcCompileOptions {
    const DxbcStreamOutput *so;
    uint32_t so_count;
    const uint32_t *so_strides;
    uint32_t so_stride_count;
    uint32_t rasterized_stream;
    /* direct3d 12 binds through the root signature instead of the stage's table. */
    int root_signature;
    const DxbcRootBinding *root;
    uint32_t root_count;
    /* direct3d 12 shaders know the render target sample count from their pipeline state. */
    uint32_t sample_count;
    /*
     * mesh pipelines link stages by semantic: mesh shader outputs go where
     * the pixel shader's inputs are, and pixel inputs the mesh shader writes
     * per primitive are per primitive.
     */
    const DxbcSignature *linked_inputs;
    const DxbcSignature *per_primitive;
    /*
     * libraries: the export name of the function to compile, and how a gpu
     * descriptor handle in a shader record gives its heap index: the index
     * times the increment, in its low handle_bits bits.
     */
    const char *export_name;
    uint32_t handle_bits, increment_shift;
    /* samples may take offsets that are not constant, as VK_KHR_maintenance8 allows. */
    int nonconstant_offsets;
    /* compute shaders take derivatives over quads, as VK_KHR_compute_shader_derivatives' linear groups. */
    int compute_derivatives;
    /* reads report whether what they read is mapped, for CheckAccessFullyMapped. */
    int residency;
} DxbcCompileOptions;

/* what a stage slot holds, as the shader declares it. */
enum {
    DXBC_SLOT_NONE,
    DXBC_SLOT_IMAGE,
    DXBC_SLOT_BUFFER,
};

typedef struct DxbcShaderInfo {
    DxbcProgram program;
    uint32_t major, minor;
    DxbcSignature inputs, outputs, patch_constants;

    /* slots the code uses. */
    uint32_t cb_mask;
    uint32_t sampler_mask;
    uint32_t srv_mask[4];
    uint64_t uav_mask;
    uint64_t uav_counter_mask;
    uint8_t srv_kind[128];
    uint8_t uav_kind[64];
    /* the compact table: the full layout entry each dword stands for. */
    uint16_t table_entries[DXBC_TABLE_DWORDS];
    uint32_t table_count;

    /* the vertex shader's inputs and the pixel shader's targets, by register. */
    uint32_t input_mask;
    uint32_t output_mask;
    int writes_depth;
    int uses_discard;

    /* geometry shaders. */
    uint32_t gs_input_primitive;
    uint32_t gs_output_topology;
    uint32_t gs_max_vertices;
    uint32_t gs_instances;
    /* hull and domain shaders. */
    uint32_t input_control_points;
    uint32_t output_control_points;
    uint32_t tess_domain;
    uint32_t tess_partitioning;
    uint32_t tess_output_primitive;
    /* compute shaders: thread counts, and the wave sizes they take, 0 for any. */
    uint32_t thread_group[3];
    uint32_t wave_min, wave_max, wave_preferred;

    uint32_t *spirv;
    size_t spirv_words;
    /* why compilation failed, for the log. */
    char error[128];
} DxbcShaderInfo;

/* the chunk with the tag, or NULL. */
const void *dxbc_find_chunk(const void *bytecode, size_t length, uint32_t tag, uint32_t *size);
int dxbc_check(const void *bytecode, size_t length);
/* the checksum as the container header stores it. */
void dxbc_checksum(const void *bytecode, size_t length, uint32_t out[4]);
/* names point into the bytecode. */
int dxbc_parse_signature(const void *bytecode, size_t length, uint32_t tag, DxbcSignature *out,
                         const MimeAllocator *alloc);
/* reads whichever chunk variant the shader has. */
int dxbc_parse_io(const void *bytecode, size_t length, int output, DxbcSignature *out, const MimeAllocator *alloc);

/* 0 on success, the reason in info->error otherwise. */
int dxbc_compile(const void *bytecode, size_t length, const DxbcCompileOptions *options, DxbcShaderInfo *info,
                 const MimeAllocator *alloc);
void dxbc_free(DxbcShaderInfo *info, const MimeAllocator *alloc);

/*
 * a geometry shader container that passes each vertex through, for stream
 * output from a vertex or domain shader. primitive is a dcl_inputprimitive
 * kind. NULL on failure.
 */
void *dxbc_passthrough_gs(const void *bytecode, size_t length, uint32_t primitive, const MimeAllocator *alloc,
                          size_t *out_length);

#endif
