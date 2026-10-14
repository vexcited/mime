/* sync objects use the gpu channel's completion fences. */
#include "opengl32.h"

typedef struct GlSync {
    struct GlSync *next;
    GlShared *shared;
    uint64_t fence;
} GlSync;

static GlSync *syncs;

static GlSync **find_sync(GlContext *ctx, GLsync handle)
{
    GlSync **link = &syncs;
    while (*link) {
        if ((GLsync)*link == handle && (*link)->shared == ctx->shared)
            return link;
        link = &(*link)->next;
    }
    return link;
}

static BOOL sync_fence(GlContext *ctx, GLsync handle, uint64_t *fence)
{
    gl_device_lock();
    GlSync *sync = *find_sync(ctx, handle);
    if (sync)
        *fence = sync->fence;
    gl_device_unlock();
    if (!sync)
        gl_error(ctx, GL_INVALID_VALUE);
    return sync != NULL;
}

GLAPI GLsync APIENTRY glFenceSync(GLenum condition, GLbitfield flags)
{
    GL_CONTEXT_OR_RETURN(NULL);
    if (condition != GL_SYNC_GPU_COMMANDS_COMPLETE || flags) {
        gl_error(ctx, condition != GL_SYNC_GPU_COMMANDS_COMPLETE ? GL_INVALID_ENUM : GL_INVALID_VALUE);
        return NULL;
    }
    GlSync *sync = gl_alloc(sizeof(*sync));
    if (!sync) {
        gl_error(ctx, GL_OUT_OF_MEMORY);
        return NULL;
    }
    gl_device_lock();
    gl_flush_clears(ctx);
    sync->fence = gl_fence();
    sync->shared = ctx->shared;
    sync->next = syncs;
    syncs = sync;
    gl_device_unlock();
    return (GLsync)sync;
}

GLAPI GLboolean APIENTRY glIsSync(GLsync handle)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    gl_device_lock();
    BOOL exists = *find_sync(ctx, handle) != NULL;
    gl_device_unlock();
    return exists;
}

GLAPI void APIENTRY glDeleteSync(GLsync handle)
{
    GL_CONTEXT_OR_RETURN();
    if (!handle)
        return;
    gl_device_lock();
    GlSync **link = find_sync(ctx, handle);
    GlSync *sync = *link;
    if (sync)
        *link = sync->next;
    gl_device_unlock();
    if (sync)
        gl_free(sync);
    else
        gl_error(ctx, GL_INVALID_VALUE);
}

GLAPI GLenum APIENTRY glClientWaitSync(GLsync handle, GLbitfield flags, GLuint64 timeout)
{
    GL_CONTEXT_OR_RETURN(GL_WAIT_FAILED);
    if (flags & ~GL_SYNC_FLUSH_COMMANDS_BIT) {
        gl_error(ctx, GL_INVALID_VALUE);
        return GL_WAIT_FAILED;
    }
    uint64_t fence;
    if (!sync_fence(ctx, handle, &fence))
        return GL_WAIT_FAILED;
    if (gl_fence_done(fence))
        return GL_ALREADY_SIGNALED;
    if (flags & GL_SYNC_FLUSH_COMMANDS_BIT) {
        gl_device_lock();
        gl_publish();
        gl_device_unlock();
    }
    if (timeout)
        gl_want(fence);
    GLuint64 start = gl_timestamp();
    while (!gl_fence_done(fence)) {
        GLuint64 elapsed = gl_timestamp() - start;
        if (elapsed >= timeout)
            return GL_TIMEOUT_EXPIRED;
        GLuint64 remaining = timeout - elapsed;
        ULONG milliseconds = remaining >= 1000000000ull ? 1000 : (ULONG)((remaining + 999999) / 1000000);
        NTSTATUS status = NtGdiMimeGpuWait(fence, milliseconds);
        if (status == STATUS_DEVICE_REMOVED || status == STATUS_INVALID_DEVICE_STATE)
            return GL_WAIT_FAILED;
    }
    return GL_CONDITION_SATISFIED;
}

GLAPI void APIENTRY glWaitSync(GLsync handle, GLbitfield flags, GLuint64 timeout)
{
    GL_CONTEXT_OR_RETURN();
    if (flags || timeout != GL_TIMEOUT_IGNORED) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    uint64_t fence;
    if (sync_fence(ctx, handle, &fence))
        gl_wait(fence);
}

GLAPI void APIENTRY glGetSynciv(GLsync handle, GLenum pname, GLsizei size, GLsizei *length, GLint *values)
{
    GL_CONTEXT_OR_RETURN();
    if (size < 0 || (size && !values)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    uint64_t fence;
    if (!sync_fence(ctx, handle, &fence))
        return;
    GLint value;
    switch (pname) {
    case GL_OBJECT_TYPE: value = GL_SYNC_FENCE; break;
    case GL_SYNC_CONDITION: value = GL_SYNC_GPU_COMMANDS_COMPLETE; break;
    case GL_SYNC_STATUS: value = gl_fence_done(fence) ? GL_SIGNALED : GL_UNSIGNALED; break;
    case GL_SYNC_FLAGS: value = 0; break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (size)
        *values = value;
    if (length)
        *length = size ? 1 : 0;
}
