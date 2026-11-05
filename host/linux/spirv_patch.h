#ifndef MIME_LINUX_SPIRV_PATCH_H
#define MIME_LINUX_SPIRV_PATCH_H

#include <mime/gpu.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SPIRV_MAX_FEEDBACK 64

typedef struct SpirvFeedback {
    uint32_t count;
    MimeGpuFeedbackInfo info[SPIRV_MAX_FEEDBACK];
    char names[SPIRV_MAX_FEEDBACK][96];
    uint32_t stride[MIME_GPU_MAX_XFB_BUFFERS]; /* bytes of a vertex in each buffer. */
    uint32_t buffers;
} SpirvFeedback;

/* fills the geometry and tessellation fields of info from the execution modes of a module. */
void spirv_modes(const uint32_t *w, size_t n, MimeGpuStageInfo *info);

/*
 * decorates the outputs named in names (NUL separated) for transform feedback and returns the new module, to be freed.
 * false with a reason in error when a name is not an output.
 */
bool spirv_feedback(const uint32_t *w, size_t n, const char *names, size_t names_len, bool separate, uint32_t **out_w,
                    size_t *out_n, SpirvFeedback *fb, char *error, size_t error_size);


/* writes the locations and indices of fragment outputs into a module, see glBindFragDataLocationIndexed. */
bool spirv_bind_outputs(const uint32_t *w, size_t n, const char *table, size_t table_len, uint32_t **out_w, size_t *out_n);

#endif
