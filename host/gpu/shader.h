#ifndef MIME_HOST_GPU_SHADER_H
#define MIME_HOST_GPU_SHADER_H

#include <mime/gpu.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct MimeShaderResult {
    bool ok;
    char *msl[MIME_GPU_STAGES];   /* metal source of each stage */
    char entry[MIME_GPU_STAGES][64];
    uint8_t *info;                /* MimeGpuProgramInfo and what it points to */
    uint32_t info_size;
} MimeShaderResult;

/* bindings holds "name\0location\0" pairs from glBindAttribLocation. the info is filled even on failure. */
bool mime_shader_compile(const char *vs, size_t vs_len, const char *fs, size_t fs_len, const char *bindings,
                         size_t bindings_len, MimeShaderResult *out);
void mime_shader_free(MimeShaderResult *r);

#endif
