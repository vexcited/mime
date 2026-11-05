/*
 * transform feedback. the varyings a program captures are named before it links, the host decorates its last vertex
 * processing stage for them, and the buffers they go to are bound to a transform feedback object. the host starts the
 * capture at the first draw after a begin and keeps byte counters per object, which glDrawTransformFeedback reads.
 */
#include "opengl32.h"

void APIENTRY glBindBuffer(GLenum target, GLuint name);

void gl_program_set_feedback_names(GlProgram *p, GLsizei count, const GLchar *const *names, GLenum mode)
{
    for (uint32_t i = 0; i < p->feedback_name_count; i++)
        gl_free(p->feedback_names[i]);
    gl_free(p->feedback_names);
    p->feedback_names = nullptr;
    p->feedback_name_count = 0;
    p->feedback_mode = mode;
    if (count <= 0)
        return;
    p->feedback_names = gl_alloc((SIZE_T)count * sizeof(char *));
    if (!p->feedback_names)
        return;
    for (GLsizei i = 0; i < count; i++)
        p->feedback_names[i] = gl_strdup(names[i]);
    p->feedback_name_count = (uint32_t)count;
}

GLAPI void APIENTRY glTransformFeedbackVaryings(GLuint program, GLsizei count, const GLchar *const *varyings,
                                                GLenum mode)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, program);
    if (!p || count < 0 || (count && !varyings)) {
        gl_error(ctx, p ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
        return;
    }
    if (mode != GL_INTERLEAVED_ATTRIBS && mode != GL_SEPARATE_ATTRIBS) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (mode == GL_SEPARATE_ATTRIBS && count > MIME_GPU_MAX_XFB_BUFFERS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    gl_program_set_feedback_names(p, count, varyings, mode);
}

GLAPI void APIENTRY glGetTransformFeedbackVarying(GLuint program, GLuint index, GLsizei size, GLsizei *length,
                                                  GLsizei *count, GLenum *type, GLchar *name)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, program);
    if (!p) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    gl_program_ready(p);
    if (index >= p->feedback_count) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    const struct GlFeedbackVarying *v = &p->feedback[index];
    SIZE_T n = strlen(v->name);
    if (size > 0 && name) {
        if (n >= (SIZE_T)size)
            n = (SIZE_T)size - 1;
        memcpy(name, v->name, n);
        name[n] = 0;
    } else {
        n = 0;
    }
    if (length)
        *length = (GLsizei)n;
    if (count)
        *count = v->size;
    if (type)
        *type = v->type;
}

/* ---------------------------------------------------------------- objects */

static GlXfb *xfb_object(GlContext *ctx, GLuint name)
{
    if (!name)
        return &ctx->st.default_xfb;
    void *x = gl_name_get(&ctx->st.xfbs, name);
    return x == GL_RESERVED ? nullptr : x;
}

static uint32_t xfb_id(GlXfb *x)
{
    if (!x->id)
        x->id = gl_new_id(ID_XFB);
    return x->id;
}

void gl_stage_context_destroy(GlContext *ctx)
{
    for (GLuint i = 0; i < ctx->st.xfbs.count; i++) {
        if (ctx->st.xfbs.items[i] && ctx->st.xfbs.items[i] != GL_RESERVED) {
            gl_free_id(ID_XFB, ((GlXfb *)ctx->st.xfbs.items[i])->id);
            gl_free(ctx->st.xfbs.items[i]);
        }
    }
    gl_free(ctx->st.xfbs.items);
    gl_free_id(ID_XFB, ctx->st.default_xfb.id);
}

GLAPI void APIENTRY glGenTransformFeedbacks(GLsizei n, GLuint *ids)
{
    GL_CONTEXT_OR_RETURN();
    if (n < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < n; i++)
        ids[i] = gl_name_new(&ctx->st.xfbs);
}

GLAPI void APIENTRY glCreateTransformFeedbacks(GLsizei n, GLuint *ids)
{
    GL_CONTEXT_OR_RETURN();
    if (n < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < n; i++) {
        ids[i] = gl_name_new(&ctx->st.xfbs);
        GlXfb *x = gl_alloc(sizeof(GlXfb));
        if (!x) {
            gl_error(ctx, GL_OUT_OF_MEMORY);
            return;
        }
        x->name = ids[i];
        gl_name_set(&ctx->st.xfbs, ids[i], x);
    }
}

GLAPI void APIENTRY glDeleteTransformFeedbacks(GLsizei n, const GLuint *ids)
{
    GL_CONTEXT_OR_RETURN();
    if (n < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < n; i++) {
        void *o = ids[i] ? gl_name_get(&ctx->st.xfbs, ids[i]) : nullptr;
        if (!o)
            continue;
        if (o != GL_RESERVED) {
            GlXfb *x = o;
            if (x->active) {
                gl_error(ctx, GL_INVALID_OPERATION);
                continue;
            }
            if (ctx->st.xfb == x)
                ctx->st.xfb = &ctx->st.default_xfb;
            gl_free_id(ID_XFB, x->id);
            gl_free(x);
        }
        gl_name_set(&ctx->st.xfbs, ids[i], nullptr);
    }
}

GLAPI GLboolean APIENTRY glIsTransformFeedback(GLuint id)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    void *o = id ? gl_name_get(&ctx->st.xfbs, id) : nullptr;
    return o && o != GL_RESERVED;
}

GLAPI void APIENTRY glBindTransformFeedback(GLenum target, GLuint id)
{
    GL_CONTEXT_OR_RETURN();
    if (target != GL_TRANSFORM_FEEDBACK) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GlXfb *cur = ctx->st.xfb ? ctx->st.xfb : &ctx->st.default_xfb;
    if (cur->active && !cur->paused) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    void *o = id ? gl_name_get(&ctx->st.xfbs, id) : &ctx->st.default_xfb;
    if (!o) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (o == GL_RESERVED) {
        GlXfb *x = gl_alloc(sizeof(GlXfb));
        if (!x) {
            gl_error(ctx, GL_OUT_OF_MEMORY);
            return;
        }
        x->name = id;
        gl_name_set(&ctx->st.xfbs, id, x);
        o = x;
    }
    ctx->st.xfb = o;
    ((GlXfb *)o)->sent_valid = FALSE;
}

/* ---------------------------------------------------------------- buffers */

static GlXfb *current(GlContext *ctx)
{
    if (!ctx->st.xfb)
        ctx->st.xfb = &ctx->st.default_xfb;
    return ctx->st.xfb;
}

static BOOL bind_to(GlContext *ctx, GlXfb *x, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size)
{
    if (x->active) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return FALSE;
    }
    if (index >= MIME_GPU_MAX_XFB_BUFFERS || (buffer && (offset < 0 || size < 0 || (offset & 3) || (size & 3)))) {
        gl_error(ctx, GL_INVALID_VALUE);
        return FALSE;
    }
    if (buffer && !gl_buffer(ctx, buffer)) {
        /* a name that was never bound gets its object, as binding it would. */
        glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, buffer);
        if (!gl_buffer(ctx, buffer))
            return FALSE;
    }
    x->buffer[index] = buffer;
    x->offset[index] = buffer ? offset : 0;
    x->size[index] = buffer ? size : 0;
    x->sent_valid = FALSE;
    return TRUE;
}

BOOL gl_xfb_bind(GlContext *ctx, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size, BOOL whole)
{
    if (whole)
        offset = 0, size = 0;
    ctx->st.xfb_buffer = buffer;
    return bind_to(ctx, current(ctx), index, buffer, offset, size);
}

void gl_xfb_buffer_deleted(GlContext *ctx, GLuint buffer)
{
    if (ctx->st.xfb_buffer == buffer)
        ctx->st.xfb_buffer = 0;
    GlXfb *list[1] = {&ctx->st.default_xfb};
    for (GLuint i = 0; i <= ctx->st.xfbs.count; i++) {
        GlXfb *x = i == 0 ? list[0] : (ctx->st.xfbs.items[i - 1] != GL_RESERVED ? ctx->st.xfbs.items[i - 1] : nullptr);
        if (!x)
            continue;
        for (int k = 0; k < MIME_GPU_MAX_XFB_BUFFERS; k++)
            if (x->buffer[k] == buffer) {
                x->buffer[k] = 0;
                x->sent_valid = FALSE;
            }
    }
}

BOOL gl_xfb_get_indexed(GlContext *ctx, GLenum target, GLuint index, GLint64 *value)
{
    if (target != GL_TRANSFORM_FEEDBACK_BUFFER_BINDING && target != GL_TRANSFORM_FEEDBACK_BUFFER_START &&
        target != GL_TRANSFORM_FEEDBACK_BUFFER_SIZE)
        return FALSE;
    if (index >= MIME_GPU_MAX_XFB_BUFFERS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return TRUE;
    }
    GlXfb *x = current(ctx);
    *value = target == GL_TRANSFORM_FEEDBACK_BUFFER_BINDING ? x->buffer[index]
             : target == GL_TRANSFORM_FEEDBACK_BUFFER_START ? x->offset[index]
                                                            : x->size[index];
    return TRUE;
}

GLAPI void APIENTRY glTransformFeedbackBufferBase(GLuint xfb, GLuint index, GLuint buffer)
{
    GL_CONTEXT_OR_RETURN();
    GlXfb *x = xfb_object(ctx, xfb);
    if (!x) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    bind_to(ctx, x, index, buffer, 0, 0);
}

GLAPI void APIENTRY glTransformFeedbackBufferRange(GLuint xfb, GLuint index, GLuint buffer, GLintptr offset,
                                                   GLsizeiptr size)
{
    GL_CONTEXT_OR_RETURN();
    GlXfb *x = xfb_object(ctx, xfb);
    if (!x || (buffer && size <= 0)) {
        gl_error(ctx, x ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
        return;
    }
    bind_to(ctx, x, index, buffer, offset, size);
}

GLAPI void APIENTRY glGetTransformFeedbackiv(GLuint xfb, GLenum pname, GLint *param)
{
    GL_CONTEXT_OR_RETURN();
    GlXfb *x = xfb_object(ctx, xfb);
    if (!x) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (pname == GL_TRANSFORM_FEEDBACK_PAUSED)
        *param = x->paused;
    else if (pname == GL_TRANSFORM_FEEDBACK_ACTIVE)
        *param = x->active;
    else
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glGetTransformFeedbacki_v(GLuint xfb, GLenum pname, GLuint index, GLint *param)
{
    GL_CONTEXT_OR_RETURN();
    GlXfb *x = xfb_object(ctx, xfb);
    if (!x || index >= MIME_GPU_MAX_XFB_BUFFERS) {
        gl_error(ctx, x ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
        return;
    }
    if (pname == GL_TRANSFORM_FEEDBACK_BUFFER_BINDING)
        *param = (GLint)x->buffer[index];
    else
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glGetTransformFeedbacki64_v(GLuint xfb, GLenum pname, GLuint index, GLint64 *param)
{
    GL_CONTEXT_OR_RETURN();
    GlXfb *x = xfb_object(ctx, xfb);
    if (!x || index >= MIME_GPU_MAX_XFB_BUFFERS) {
        gl_error(ctx, x ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
        return;
    }
    if (pname == GL_TRANSFORM_FEEDBACK_BUFFER_START)
        *param = x->offset[index];
    else if (pname == GL_TRANSFORM_FEEDBACK_BUFFER_SIZE)
        *param = x->size[index];
    else
        gl_error(ctx, GL_INVALID_ENUM);
}

/* ---------------------------------------------------------------- capturing */

static void control(GlXfb *x, uint32_t action)
{
    gl_device_lock();
    MimeGpuXfbControl *c = gl_emit(MIME_GPU_XFB_CONTROL, sizeof(MimeGpuXfbControl));
    c->action = action;
    c->object = xfb_id(x);
    gl_device_unlock();
}

/* the buffers the capture writes, sent when they changed and marked as read by the gpu. the device lock is held. */
void gl_xfb_prepare(GlContext *ctx)
{
    GlXfb *x = ctx->st.xfb;
    if (!x || !x->active || x->paused)
        return;
    MimeGpuMem mem[MIME_GPU_MAX_XFB_BUFFERS] = {};
    uint64_t size[MIME_GPU_MAX_XFB_BUFFERS] = {};
    BOOL changed = !x->sent_valid;
    for (uint32_t i = 0; i < x->count; i++) {
        GlBuffer *b = gl_buffer(ctx, x->buffer[i]);
        if (!b || !b->mem.ptr || (uint64_t)x->offset[i] > b->size)
            continue;
        b->last_use = gl_batch_fence();
        mem[i] = gl_mem(&b->mem);
        mem[i].offset += (uint64_t)x->offset[i];
        size[i] = x->size[i] ? (uint64_t)x->size[i] : b->size - (uint64_t)x->offset[i];
        size[i] &= ~3ull;
        if (memcmp(&mem[i], &x->sent[i], sizeof(mem[i])) || size[i] != x->sent_size[i])
            changed = TRUE;
    }
    if (!changed)
        return;
    MimeGpuXfbBuffers *c = gl_emit(MIME_GPU_XFB_BUFFERS, sizeof(MimeGpuXfbBuffers));
    c->count = x->count;
    for (uint32_t i = 0; i < MIME_GPU_MAX_XFB_BUFFERS; i++) {
        c->buffer[i].mem = mem[i];
        c->buffer[i].size = size[i];
        x->sent[i] = mem[i];
        x->sent_size[i] = size[i];
    }
    x->sent_valid = TRUE;
}

GLAPI void APIENTRY glBeginTransformFeedback(GLenum mode)
{
    GL_CONTEXT_OR_RETURN();
    GlXfb *x = current(ctx);
    GlProgram *p = gl_draw_program(ctx);
    if (mode != GL_POINTS && mode != GL_LINES && mode != GL_TRIANGLES) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (x->active || !p || !gl_program_ready(p) || !p->feedback_count || !p->stage.feedback_buffers) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    for (uint32_t i = 0; i < p->stage.feedback_buffers; i++) {
        if (!gl_buffer(ctx, x->buffer[i])) {
            gl_error(ctx, GL_INVALID_OPERATION);
            return;
        }
    }
    x->mode = mode;
    x->count = p->stage.feedback_buffers;
    x->stride = p->stage.feedback_stride[0];
    x->active = TRUE;
    x->paused = FALSE;
    x->sent_valid = FALSE;
    control(x, MIME_GPU_XFB_BEGIN);
    /* the buffers go with the first draw, which knows whether they moved. */
    ctx->dirty |= DIRTY_PIPELINE;
}

GLAPI void APIENTRY glEndTransformFeedback(void)
{
    GL_CONTEXT_OR_RETURN();
    GlXfb *x = current(ctx);
    if (!x->active) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (!x->paused)
        control(x, MIME_GPU_XFB_END);
    x->active = x->paused = FALSE;
}

GLAPI void APIENTRY glPauseTransformFeedback(void)
{
    GL_CONTEXT_OR_RETURN();
    GlXfb *x = current(ctx);
    if (!x->active || x->paused) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    x->paused = TRUE;
    control(x, MIME_GPU_XFB_PAUSE);
}

GLAPI void APIENTRY glResumeTransformFeedback(void)
{
    GL_CONTEXT_OR_RETURN();
    GlXfb *x = current(ctx);
    if (!x->active || !x->paused) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    x->paused = FALSE;
    x->sent_valid = FALSE;
    control(x, MIME_GPU_XFB_RESUME);
}

/* ---------------------------------------------------------------- drawing what was captured */

static void draw_feedback(GLenum mode, GLuint id, GLuint stream, GLsizei instances)
{
    GL_CONTEXT_OR_RETURN();
    GlXfb *x = xfb_object(ctx, id);
    if (!x || !x->id || !x->stride) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (stream >= 1 || instances < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!gl_draw_mode_valid(ctx, mode)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (x->active && !x->paused) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (instances)
        gl_draw_feedback(ctx, mode, x->id, 0, x->stride, instances);
}

GLAPI void APIENTRY glDrawTransformFeedback(GLenum mode, GLuint id)
{
    draw_feedback(mode, id, 0, 1);
}

GLAPI void APIENTRY glDrawTransformFeedbackStream(GLenum mode, GLuint id, GLuint stream)
{
    draw_feedback(mode, id, stream, 1);
}

GLAPI void APIENTRY glDrawTransformFeedbackInstanced(GLenum mode, GLuint id, GLsizei instances)
{
    draw_feedback(mode, id, 0, instances);
}

GLAPI void APIENTRY glDrawTransformFeedbackStreamInstanced(GLenum mode, GLuint id, GLuint stream, GLsizei instances)
{
    draw_feedback(mode, id, stream, instances);
}
