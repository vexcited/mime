#ifndef MIME_HOST_GPU_SHADER_H
#define MIME_HOST_GPU_SHADER_H

#include <mime/gpu.h>
#include <stddef.h>

/*
 * Vulkan resources use descriptor set 0: loose uniforms at binding 0, named blocks at 1 to 12, then stage textures
 * from MIME_SHADER_TEXTURE_BINDING. each sampler has one binding, including its array elements.
 */
#define MIME_SHADER_TEXTURE_BINDING(stage, slot) (16u + (uint32_t)(stage) * MIME_GPU_MAX_TEXTURES + (uint32_t)(slot))

enum { MIME_SHADER_UNIFORMS, MIME_SHADER_BLOCK, MIME_SHADER_TEXTURE };

typedef struct MimeShaderBinding {
    uint32_t binding;
    uint32_t count;  /* descriptors. */
    uint32_t stages; /* 1 << MIME_GPU_STAGE_*. */
    uint32_t kind;   /* MIME_SHADER_* */
    uint32_t slot;   /* textures: the first texture slot of their stage. */
    uint32_t gl_type; /* textures: the sampler type. */
    uint32_t bytes;   /* uniform buffers: the size of the block. */
} MimeShaderBinding;

/* the stages a program has besides the vertex and fragment ones. */
typedef struct MimeShaderStages {
    const char *control, *evaluation, *geometry;
    size_t control_len, evaluation_len, geometry_len;
} MimeShaderStages;

enum { MIME_SHADER_CONTROL, MIME_SHADER_EVALUATION, MIME_SHADER_GEOMETRY, MIME_SHADER_EXTRAS };

/* bits of MimeShaderBinding.stages and of the stages of a uniform block. */
enum {
    MIME_SHADER_BIT_VERTEX = 1 << 0,
    MIME_SHADER_BIT_FRAGMENT = 1 << 1,
    MIME_SHADER_BIT_CONTROL = 1 << 2,
    MIME_SHADER_BIT_EVALUATION = 1 << 3,
    MIME_SHADER_BIT_GEOMETRY = 1 << 4,
};

typedef struct MimeShaderResult {
    bool ok;
    char *msl[MIME_GPU_STAGES];   /* Metal source of each stage. */
    char entry[MIME_GPU_STAGES][64];
    uint32_t *spirv[MIME_GPU_STAGES]; /* Vulkan: the SPIR-V of each stage. */
    size_t spirv_words[MIME_GPU_STAGES];
    uint32_t *spirv_extra[MIME_SHADER_EXTRAS]; /* Vulkan: control, evaluation and geometry. */
    size_t extra_words[MIME_SHADER_EXTRAS];
    MimeShaderBinding *bindings;  /* Vulkan: what descriptor set 0 holds. */
    uint32_t binding_count;
    uint8_t *info;                /* MimeGpuProgramInfo and what it points to. */
    uint32_t info_size;
} MimeShaderResult;

/* bindings holds "name\0location\0" pairs from glBindAttribLocation. the info is filled even on failure. */
bool mime_shader_compile(const char *vs, size_t vs_len, const char *fs, size_t fs_len, const char *bindings,
                         size_t bindings_len, MimeShaderResult *out);
/* Vulkan only: the same with the stages of extra, which may be null. */
bool mime_shader_compile_stages(const char *vs, size_t vs_len, const char *fs, size_t fs_len,
                                const MimeShaderStages *extra, const char *bindings, size_t bindings_len,
                                MimeShaderResult *out);
void mime_shader_free(MimeShaderResult *r);

#endif
