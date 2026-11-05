#ifndef MIME_HOST_GPU_SHADER_H
#define MIME_HOST_GPU_SHADER_H

#include <mime/gpu.h>
#include <stddef.h>

/*
 * Vulkan resources use descriptor set 0: loose uniforms at binding 0, named blocks at 1 to 12, then stage textures
 * from MIME_SHADER_TEXTURE_BINDING. each sampler has one binding, including its array elements.
 */
#define MIME_SHADER_TEXTURE_BINDING(stage, slot) (16u + (uint32_t)(stage) * MIME_GPU_MAX_TEXTURES + (uint32_t)(slot))

/*
 * storage buffers (a block each), images (an image each) and atomic counter buffers have bindings far above the others,
 * so that more stages and more textures can be added below them.
 */
#define MIME_SHADER_STORAGE_BINDING(slot) (512u + (uint32_t)(slot))
#define MIME_SHADER_IMAGE_BINDING(slot) (544u + (uint32_t)(slot))
#define MIME_SHADER_COUNTER_BINDING(slot) (576u + (uint32_t)(slot))
#define MIME_GPU_MAX_COUNTER_BUFFERS 8

enum { MIME_SHADER_UNIFORMS, MIME_SHADER_BLOCK, MIME_SHADER_TEXTURE, MIME_SHADER_STORAGE, MIME_SHADER_IMAGE,
       MIME_SHADER_COUNTER };

typedef struct MimeShaderBinding {
    uint32_t binding;
    uint32_t count;  /* descriptors. */
    uint32_t stages; /* 1 << MIME_GPU_STAGE_*. */
    uint32_t kind;   /* MIME_SHADER_* */
    uint32_t slot;   /* textures: the first texture slot of their stage. */
    uint32_t gl_type; /* textures: the sampler type. */
    uint32_t bytes;   /* uniform buffers: the size of the block. */
} MimeShaderBinding;

typedef struct MimeShaderResult {
    bool ok;
    char *msl[MIME_GPU_STAGES];   /* Metal source of each stage. */
    char entry[MIME_GPU_STAGES][64];
    uint32_t *spirv[MIME_GPU_STAGES]; /* Vulkan: the SPIR-V of each stage. */
    size_t spirv_words[MIME_GPU_STAGES];
    MimeShaderBinding *bindings;  /* Vulkan: what descriptor set 0 holds. */
    uint32_t binding_count;
    uint8_t *info;                /* MimeGpuProgramInfo and what it points to. */
    uint32_t info_size;
} MimeShaderResult;

/* bindings holds "name\0location\0" pairs from glBindAttribLocation. the info is filled even on failure. */
bool mime_shader_compile(const char *vs, size_t vs_len, const char *fs, size_t fs_len, const char *bindings,
                         size_t bindings_len, MimeShaderResult *out);
/* a compute program: the SPIR-V is spirv[0], its resources use stage 0 of the bindings. */
bool mime_shader_compile_compute(const char *cs, size_t cs_len, MimeShaderResult *out);
void mime_shader_free(MimeShaderResult *r);

#endif
