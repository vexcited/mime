/* what msl.cpp shares with the C files: the driver's Metal buffer slots and the SPIR-V to MSL call. */
#ifndef MIME_VKMETAL_VKM_MSL_H
#define MIME_VKMETAL_VKM_MSL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VKM_SETS 8
#define VKM_BUFFER_PUSH 8
#define VKM_BUFFER_AUX 9
#define VKM_BUFFER_VERTEX 10
#define VKM_VERTEX_BINDINGS 21
#define VKM_VERTEX_ATTRIBUTES 31
#define VKM_COLORS 8
#define VKM_VIEWPORTS 16
#define VKM_PUSH_SIZE 256
#define VKM_TEXTURE_SLOTS 128
/* the descriptor set of the buffers the geometry stage's emulation binds, each a buffer of its own. */
#define VKM_EMU_SET 31

/* a compute shader's threadgroup size, as MTLSize has it, which C++ cannot include. */
typedef struct VkmSize {
    size_t width, height, depth;
} VkmSize;

typedef struct VkmInput {
    uint8_t location, components;
    uint8_t kind; /* 0 float, 1 signed, 2 unsigned. */
} VkmInput;

/* msl.cpp: SPIR-V to MSL with argument buffers laid out like the set layouts. */
typedef struct VkmMslBinding {
    uint32_t set, binding, count, slot, sampler_slot;
    uint8_t kind; /* VKM_MSL_*. */
} VkmMslBinding;

enum { VKM_MSL_BUFFER, VKM_MSL_IMAGE, VKM_MSL_SAMPLER, VKM_MSL_COMBINED };

typedef struct VkmMslSpec {
    uint32_t id, size;
    uint64_t value;
} VkmMslSpec;

typedef struct VkmMslResult {
    char *source; /* malloced. */
    char entry[128];
    char error[256];
    VkmSize local;
    uint32_t inputs;
    VkmInput input[VKM_VERTEX_ATTRIBUTES];
    uint32_t outputs;
    bool writes_depth, writes_stencil, discards;
} VkmMslResult;

/*
 * the vertex attributes narrower than a shader's signed input reads them, VKM_WIDEN_* per location, or null. with
 * dual_source a fragment shader's output 1 is the second source of output 0's blending.
 */
enum { VKM_WIDEN_NONE, VKM_WIDEN_UINT8, VKM_WIDEN_UINT16 };

/*
 * accel.m: the table shaders find acceleration structures in by address, its address and its entry count less one.
 * a descriptor or a conversion from an address gives a structure as that address.
 */
void vkm_as_table(uint64_t *address, uint32_t *mask);

bool vkm_msl(const uint32_t *spirv, size_t words, const char *entry, int model, const VkmMslBinding *bindings,
             uint32_t binding_count, uint32_t set_mask, const VkmMslSpec *spec, uint32_t spec_count,
             const uint8_t *widen, bool dual_source, VkmMslResult *out);

#endif
