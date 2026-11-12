#ifndef MIME_VKMETAL_RAYTRACE_H
#define MIME_VKMETAL_RAYTRACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <vulkan/vulkan.h>

#include "vkm_msl.h"

/* the deepest recursion a pipeline gets, which is direct3d's most. */
#define VKM_RT_MAX_DEPTH 31
/* how deep callable shaders call callable shaders, past which a call runs nothing. */
#define VKM_RT_CALLABLE_DEPTH 4
/* a group's handle: its id, then zeros. */
#define VKM_RT_HANDLE_SIZE 32
/* the kernel's parameters are the first buffer of the emulation set. */
#define VKM_RT_SET VKM_EMU_SET
#define VKM_RT_LOCAL_X 8
#define VKM_RT_LOCAL_Y 8

/* what an any-hit shader decided. */
enum { VKM_RT_ACCEPT, VKM_RT_IGNORE, VKM_RT_TERMINATE };

typedef struct VkmRtStage {
    const uint32_t *code;
    size_t words;
    const char *entry;
    VkShaderStageFlagBits stage;
    const VkSpecializationInfo *spec;
} VkmRtStage;

typedef struct VkmRtGroup {
    uint32_t id; /* never 0, which is a record of no group. */
    VkRayTracingShaderGroupTypeKHR type;
    uint32_t general, closest, any, intersection; /* stages, or VK_SHADER_UNUSED_KHR. */
} VkmRtGroup;

/*
 * the kernel of a pipeline: GLCompute, entry point main, threadgroups of VKM_RT_LOCAL_X by VKM_RT_LOCAL_Y. it reads
 * its parameters from binding 0 of set VKM_RT_SET: the shader binding tables' addresses and strides, a mode, the
 * extent and the indirect command's address. the mode is 0 for a direct trace, 1 for vkCmdTraceRaysIndirectKHR and
 * 2 for its second version.
 */
bool vkm_rt_link(const VkmRtStage *stages, uint32_t stage_count, const VkmRtGroup *groups, uint32_t group_count,
                 uint32_t depth, uint32_t **out, size_t *out_words);

typedef struct VkmRtParams {
    uint64_t raygen, miss, hit, callable;
    uint32_t miss_stride, hit_stride, callable_stride, mode;
    uint32_t width, height, depth, pad;
    uint64_t indirect;
} VkmRtParams;

#endif
