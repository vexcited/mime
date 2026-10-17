#ifndef MIME_HOST_GPU_SHADER_H
#define MIME_HOST_GPU_SHADER_H

#include <mime/gpu.h>
#include <stddef.h>

/*
 * on vulkan every resource of a program is in descriptor set 0: the loose
 * uniforms at binding 0, named blocks at 1 to 12, then the textures of each
 * stage from MIME_SHADER_TEXTURE_BINDING, a binding per sampler, arrays
 * taking their texture slots in one binding.
 */
#define MIME_SHADER_TEXTURE_BINDING(stage, slot) (16u + (uint32_t)(stage) * MIME_GPU_MAX_TEXTURES + (uint32_t)(slot))

enum { MIME_SHADER_UNIFORMS, MIME_SHADER_BLOCK, MIME_SHADER_TEXTURE };

typedef struct MimeShaderBinding {
    uint32_t binding;
    uint32_t count;  /* descriptors */
    uint32_t stages; /* 1 << MIME_GPU_STAGE_* */
    uint32_t kind;   /* MIME_SHADER_* */
    uint32_t slot;   /* textures: the first texture slot of their stage */
    uint32_t gl_type; /* textures: the sampler type */
    uint32_t bytes;   /* uniform buffers: the size of the block */
} MimeShaderBinding;

typedef struct MimeShaderResult {
    bool ok;
    char *msl[MIME_GPU_STAGES];   /* metal source of each stage */
    char entry[MIME_GPU_STAGES][64];
    uint32_t *spirv[MIME_GPU_STAGES]; /* vulkan: the spir-v of each stage */
    size_t spirv_words[MIME_GPU_STAGES];
    MimeShaderBinding *bindings;  /* vulkan: what descriptor set 0 holds */
    uint32_t binding_count;
    uint8_t *info;                /* MimeGpuProgramInfo and what it points to */
    uint32_t info_size;
} MimeShaderResult;

/* bindings holds "name\0location\0" pairs from glBindAttribLocation. the info is filled even on failure. */
bool mime_shader_compile(const char *vs, size_t vs_len, const char *fs, size_t fs_len, const char *bindings,
                         size_t bindings_len, MimeShaderResult *out);
void mime_shader_free(MimeShaderResult *r);

#endif
