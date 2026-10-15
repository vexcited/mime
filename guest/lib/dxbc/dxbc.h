/*
 * dxbc: the bytecode of direct3d 10 and 11 shaders, read from its
 * container and compiled to spir-v for vulkan. freestanding, so the guest
 * dlls and the host's tests build the same code.
 *
 * the spir-v follows mime's binding model. descriptor set 0 is the heap,
 * one array per kind of descriptor, which every view and sampler has an
 * index in. set 1 holds a table of heap indices per stage, a uniform
 * buffer of DXBC_TABLE_DWORDS dwords at binding = stage, which the device
 * fills with the indices of what is bound to each slot.
 */
#ifndef MIME_LIB_DXBC_H
#define MIME_LIB_DXBC_H

#include <stddef.h>
#include <stdint.h>

#include "../alloc.h"

#define DXBC_FOURCC(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)

/* the heap's bindings in set 0. */
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

/* where each kind of slot starts in a stage's table, in dwords. */
#define DXBC_TABLE_CB 0
#define DXBC_TABLE_SAMPLER 14
#define DXBC_TABLE_SRV 30
#define DXBC_TABLE_UAV 158
/* the counters of append and consume views, storage texel buffers. */
#define DXBC_TABLE_UAV_COUNTER 222
#define DXBC_TABLE_DWORDS 288

typedef enum DxbcProgram {
    DXBC_PIXEL = 0,
    DXBC_VERTEX = 1,
    DXBC_GEOMETRY = 2,
    DXBC_HULL = 3,
    DXBC_DOMAIN = 4,
    DXBC_COMPUTE = 5,
} DxbcProgram;

/* a signature element, as the ISGN, OSGN and PCSG chunks and their variants hold them. */
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

typedef struct DxbcCompileOptions {
    const DxbcStreamOutput *so;
    uint32_t so_count;
    const uint32_t *so_strides;
    uint32_t so_stride_count;
    uint32_t rasterized_stream;
} DxbcCompileOptions;

/* what a slot of a stage holds, as the shader declares it. */
enum {
    DXBC_SLOT_NONE,
    DXBC_SLOT_IMAGE,
    DXBC_SLOT_BUFFER,
};

typedef struct DxbcShaderInfo {
    DxbcProgram program;
    uint32_t major, minor;
    DxbcSignature inputs, outputs, patch_constants;

    /* the slots the code uses. */
    uint32_t cb_mask;
    uint32_t sampler_mask;
    uint32_t srv_mask[4];
    uint64_t uav_mask;
    uint64_t uav_counter_mask;
    uint8_t srv_kind[128];
    uint8_t uav_kind[64];

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
    /* compute shaders. */
    uint32_t thread_group[3];

    uint32_t *spirv;
    size_t spirv_words;
    /* why compilation failed, for the log. */
    char error[128];
} DxbcShaderInfo;

/* the chunk of the container with the tag, or NULL. */
const void *dxbc_find_chunk(const void *bytecode, size_t length, uint32_t tag, uint32_t *size);
/* checks the container's header and size. */
int dxbc_check(const void *bytecode, size_t length);
/* the checksum of the container, as its header stores it. */
void dxbc_checksum(const void *bytecode, size_t length, uint32_t out[4]);
/* the signature in the chunk with the tag, names pointing into the bytecode. */
int dxbc_parse_signature(const void *bytecode, size_t length, uint32_t tag, DxbcSignature *out,
                         const MimeAllocator *alloc);
/* the input or output signature, whichever variant of the chunk the shader has. */
int dxbc_parse_io(const void *bytecode, size_t length, int output, DxbcSignature *out, const MimeAllocator *alloc);

/* compiles the shader; 0 on success, the reason in info->error otherwise. */
int dxbc_compile(const void *bytecode, size_t length, const DxbcCompileOptions *options, DxbcShaderInfo *info,
                 const MimeAllocator *alloc);
void dxbc_free(DxbcShaderInfo *info, const MimeAllocator *alloc);

#endif
