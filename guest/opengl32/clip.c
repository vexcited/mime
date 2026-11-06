/*
 * glClipControl, the clamped polygon offset and the texture barrier. clip control picks the corner of the window the
 * clip space origin maps to and the depth range of clip space. the host takes the corner as a viewport with a negative
 * height, which flips the rows and with them the winding the rasterizer sees, and the depth range as a variant of its
 * pipelines, see MIME_GPU_FEATURE_CLIP_CONTROL.
 */
#include "opengl32.h"

GLAPI void APIENTRY glClipControl(GLenum origin, GLenum depth)
{
    GL_CONTEXT_OR_RETURN();
    if ((origin != GL_LOWER_LEFT && origin != GL_UPPER_LEFT) ||
        (depth != GL_NEGATIVE_ONE_TO_ONE && depth != GL_ZERO_TO_ONE)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->clip_upper_left = origin == GL_UPPER_LEFT;
    ctx->clip_zero_to_one = depth == GL_ZERO_TO_ONE;
    ctx->dirty |= DIRTY_RASTER | DIRTY_VIEWPORT;
}

GLAPI void APIENTRY glPolygonOffsetClamp(GLfloat factor, GLfloat units, GLfloat clamp)
{
    GL_CONTEXT_OR_RETURN();
    glPolygonOffset(factor, units);
    ctx->offset_clamp = clamp;
}

GLAPI void APIENTRY glPolygonOffsetClampEXT(GLfloat factor, GLfloat units, GLfloat clamp)
{
    glPolygonOffsetClamp(factor, units, clamp);
}

/* what a pass rendered is visible to the draws after it, which is the one thing a pass boundary guarantees. */
GLAPI void APIENTRY glTextureBarrier(void)
{
    GL_CONTEXT_OR_RETURN();
    gl_device_lock();
    gl_flush_clears(ctx);
    gl_end_pass(ctx);
    if (gl_host_has(MIME_GPU_FEATURE_COMPUTE))
        gl_emit(MIME_GPU_MEMORY_BARRIER, sizeof(MimeGpuCmd));
    gl_device_unlock();
}
