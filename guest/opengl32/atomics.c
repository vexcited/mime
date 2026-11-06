/*
 * atomic counters, GL_ARB_shader_atomic_counters: the host keeps the counters of a binding in a storage block, see
 * host/gpu/glsl_atomics.h, so the buffer bound to the binding is what the shader counts in. what is here are the limits
 * and the queries about the counter buffers of a program.
 */
#include "opengl32.h"

int gl_atomics_get(GlContext *ctx, GLenum pname, GLdouble *v)
{
    if (!gl_host_has(MIME_GPU_FEATURE_COMPUTE))
        return 0;
    switch (pname) {
    case GL_ATOMIC_COUNTER_BUFFER_BINDING:
        v[0] = ctx->atomic_buffer;
        return 1;
    case GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS:
    case GL_MAX_VERTEX_ATOMIC_COUNTER_BUFFERS:
    case GL_MAX_FRAGMENT_ATOMIC_COUNTER_BUFFERS:
    case GL_MAX_COMPUTE_ATOMIC_COUNTER_BUFFERS:
    case GL_MAX_TESS_CONTROL_ATOMIC_COUNTER_BUFFERS:
    case GL_MAX_TESS_EVALUATION_ATOMIC_COUNTER_BUFFERS:
    case GL_MAX_GEOMETRY_ATOMIC_COUNTER_BUFFERS:
        v[0] = MIME_GPU_MAX_ATOMIC_BINDINGS;
        return 1;
    case GL_MAX_COMBINED_ATOMIC_COUNTER_BUFFERS:
        v[0] = MIME_GPU_MAX_ATOMIC_BINDINGS;
        return 1;
    case GL_MAX_VERTEX_ATOMIC_COUNTERS:
    case GL_MAX_FRAGMENT_ATOMIC_COUNTERS:
    case GL_MAX_COMPUTE_ATOMIC_COUNTERS:
    case GL_MAX_TESS_CONTROL_ATOMIC_COUNTERS:
    case GL_MAX_TESS_EVALUATION_ATOMIC_COUNTERS:
    case GL_MAX_GEOMETRY_ATOMIC_COUNTERS:
    case GL_MAX_COMBINED_ATOMIC_COUNTERS:
        v[0] = 1024;
        return 1;
    case GL_MAX_ATOMIC_COUNTER_BUFFER_SIZE:
        v[0] = 1 << 16;
        return 1;
    default:
        return 0;
    }
}

BOOL gl_atomics_get_indexed(GlContext *ctx, GLenum target, GLuint index, GLint *data)
{
    if (!gl_host_has(MIME_GPU_FEATURE_COMPUTE))
        return FALSE;
    if (target != GL_ATOMIC_COUNTER_BUFFER_BINDING && target != GL_ATOMIC_COUNTER_BUFFER_START &&
        target != GL_ATOMIC_COUNTER_BUFFER_SIZE)
        return FALSE;
    if (index >= MIME_GPU_MAX_ATOMIC_BINDINGS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return TRUE;
    }
    const GlBufferBinding *b = &ctx->atomic_bindings[index];
    GlBuffer *buffer = gl_buffer(ctx, b->name);
    *data = target == GL_ATOMIC_COUNTER_BUFFER_BINDING ? (GLint)b->name
            : target == GL_ATOMIC_COUNTER_BUFFER_START ? (GLint)b->offset
                                                       : (GLint)(b->whole && buffer ? buffer->size : b->size);
    return TRUE;
}

GLAPI void APIENTRY glGetActiveAtomicCounterBufferiv(GLuint program, GLuint index, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, program);
    if (!p) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!gl_program_ready(p)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (index >= p->counter_buffer_count) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    const GlCounterBuffer *buffer = &p->counter_buffers[index];
    GLint value = 0;
    switch (pname) {
    case GL_ATOMIC_COUNTER_BUFFER_BINDING:
        value = (GLint)buffer->binding;
        break;
    case GL_ATOMIC_COUNTER_BUFFER_DATA_SIZE:
        value = (GLint)buffer->bytes;
        break;
    case GL_ATOMIC_COUNTER_BUFFER_ACTIVE_ATOMIC_COUNTERS:
        value = (GLint)buffer->counters;
        break;
    case GL_ATOMIC_COUNTER_BUFFER_ACTIVE_ATOMIC_COUNTER_INDICES: {
        /* the counters are not among the active uniforms, so their indices are their places in the list of the program. */
        GLint n = 0;
        for (uint32_t i = 0; i < p->counter_count; i++)
            if (p->counters[i].binding == buffer->binding)
                params[n++] = (GLint)i;
        return;
    }
    case GL_ATOMIC_COUNTER_BUFFER_REFERENCED_BY_VERTEX_SHADER:
        value = !p->compute && (buffer->stages & 1);
        break;
    case GL_ATOMIC_COUNTER_BUFFER_REFERENCED_BY_FRAGMENT_SHADER:
        value = !p->compute && (buffer->stages & 2);
        break;
    case GL_ATOMIC_COUNTER_BUFFER_REFERENCED_BY_TESS_CONTROL_SHADER:
        value = (buffer->stages & 4) != 0;
        break;
    case GL_ATOMIC_COUNTER_BUFFER_REFERENCED_BY_TESS_EVALUATION_SHADER:
        value = (buffer->stages & 8) != 0;
        break;
    case GL_ATOMIC_COUNTER_BUFFER_REFERENCED_BY_GEOMETRY_SHADER:
        value = (buffer->stages & 16) != 0;
        break;
    case GL_ATOMIC_COUNTER_BUFFER_REFERENCED_BY_COMPUTE_SHADER:
        value = p->compute;
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    *params = value;
}
