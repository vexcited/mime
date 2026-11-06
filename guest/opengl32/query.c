/*
 * queries. the timer ones read a monotonic clock after the submitted gpu work finished. occlusion and transform
 * feedback queries run on the host, which sums what the gpu counted into a few words of the heap and sets a flag when
 * they are final.
 */
#include "opengl32.h"

typedef struct GlQuery {
    GLenum target;
    GLuint64 value;
    BOOL ready;
    BOOL host;       /* counted by the gpu. */
    BOOL pending;    /* ended, the result is not read yet. */
    uint32_t id;     /* the host's number for it. */
    uint32_t type;   /* MIME_GPU_QUERY_*. */
    GLuint stream;
    GlMem mem;       /* the result and its flag. */
} GlQuery;

enum { SLOT_SAMPLES, SLOT_ANY, SLOT_CONSERVATIVE, SLOT_PRIMITIVES, SLOT_WRITTEN, SLOT_OVERFLOW, SLOT_ALL_OVERFLOW, SLOT_TIME };

GLuint64 gl_timestamp(void)
{
    LARGE_INTEGER counter, frequency;
    if (!NT_SUCCESS(NtQueryPerformanceCounter(&counter, &frequency)) || frequency.QuadPart <= 0)
        return 0;
    return (GLuint64)(counter.QuadPart / frequency.QuadPart) * 1000000000ull +
           (GLuint64)(counter.QuadPart % frequency.QuadPart) * 1000000000ull / frequency.QuadPart;
}

/* the slot of a query target in the active table and the host's type of it, or -1. */
static int slot_of(GLenum target, uint32_t *type)
{
    switch (target) {
    case GL_SAMPLES_PASSED:
        *type = MIME_GPU_QUERY_SAMPLES;
        return SLOT_SAMPLES;
    case GL_ANY_SAMPLES_PASSED:
        *type = MIME_GPU_QUERY_ANY_SAMPLES;
        return SLOT_ANY;
    case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
        *type = MIME_GPU_QUERY_ANY_SAMPLES;
        return SLOT_CONSERVATIVE;
    case GL_PRIMITIVES_GENERATED:
        *type = MIME_GPU_QUERY_PRIMITIVES;
        return SLOT_PRIMITIVES;
    case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
        *type = MIME_GPU_QUERY_WRITTEN;
        return SLOT_WRITTEN;
    case GL_TRANSFORM_FEEDBACK_STREAM_OVERFLOW:
        *type = MIME_GPU_QUERY_OVERFLOW;
        return SLOT_OVERFLOW;
    case GL_TRANSFORM_FEEDBACK_OVERFLOW:
        *type = MIME_GPU_QUERY_OVERFLOW;
        return SLOT_ALL_OVERFLOW;
    default:
        return -1;
    }
}

static BOOL host_target_ok(GLenum target)
{
    uint32_t type;
    if (slot_of(target, &type) < 0)
        return FALSE;
    return type <= MIME_GPU_QUERY_ANY_SAMPLES ? gl_host_has(MIME_GPU_FEATURE_QUERY) : gl_host_has(MIME_GPU_FEATURE_FEEDBACK);
}

BOOL gl_query_target_known(GLenum target)
{
    return target == GL_TIMESTAMP || target == GL_TIME_ELAPSED || host_target_ok(target);
}

static void release_query(GlQuery *q)
{
    if (q->host && q->id)
        gl_free_id(ID_QUERY, q->id);
    gl_heap_free(&q->mem, gl_batch_fence());
    gl_free(q);
}

void gl_queries_destroy(GlContext *ctx)
{
    for (GLuint i = 0; i < ctx->queries.count; i++)
        if (ctx->queries.items[i] != GL_RESERVED && ctx->queries.items[i])
            release_query(ctx->queries.items[i]);
    gl_free(ctx->queries.items);
}

static GlQuery *query(GlContext *ctx, GLuint name, GLenum target)
{
    void *object = gl_name_get(&ctx->queries, name);
    if (!object) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return nullptr;
    }
    if (object == GL_RESERVED) {
        object = gl_alloc(sizeof(GlQuery));
        if (!object) {
            gl_error(ctx, GL_OUT_OF_MEMORY);
            return nullptr;
        }
        ((GlQuery *)object)->target = target;
        gl_name_set(&ctx->queries, name, object);
    }
    GlQuery *result = object;
    if (result->target != target) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return nullptr;
    }
    return result;
}

GLAPI void APIENTRY glGenQueries(GLsizei count, GLuint *names)
{
    GL_CONTEXT_OR_RETURN();
    if (count < 0 || (count && !names)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < count; i++) {
        names[i] = gl_name_new(&ctx->queries);
        if (!names[i])
            gl_error(ctx, GL_OUT_OF_MEMORY);
    }
}

GLAPI void APIENTRY glDeleteQueries(GLsizei count, const GLuint *names)
{
    GL_CONTEXT_OR_RETURN();
    if (count < 0 || (count && !names)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < count; i++) {
        void *object = gl_name_get(&ctx->queries, names[i]);
        if (!object)
            continue;
        if (ctx->elapsed_query == names[i])
            ctx->elapsed_query = 0;
        for (int s = 0; s < 8; s++)
            for (int k = 0; k < 4; k++)
                if (ctx->st.active_query[s][k] == names[i])
                    ctx->st.active_query[s][k] = 0;
        if (ctx->st.condition == names[i])
            ctx->st.condition = 0, ctx->st.skip = FALSE;
        if (object != GL_RESERVED) {
            GlQuery *q = object;
            if (q->host && q->pending) {
                /* the host writes the result when the work is done: the memory must stay until then. */
                gl_wait(gl_fence());
            }
            release_query(q);
        }
        gl_name_set(&ctx->queries, names[i], nullptr);
    }
}

GLAPI GLboolean APIENTRY glIsQuery(GLuint name)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    void *object = gl_name_get(&ctx->queries, name);
    return object && object != GL_RESERVED && ((GlQuery *)object)->target;
}

GLAPI void APIENTRY glCreateQueries(GLenum target, GLsizei count, GLuint *names)
{
    GL_CONTEXT_OR_RETURN();
    if (!gl_query_target_known(target)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    glGenQueries(count, names);
    for (GLsizei i = 0; i < count && names; i++)
        query(ctx, names[i], target);
}

GLAPI void APIENTRY glQueryCounter(GLuint name, GLenum target)
{
    GL_CONTEXT_OR_RETURN();
    if (target != GL_TIMESTAMP) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GlQuery *object = query(ctx, name, target);
    if (!object)
        return;
    glFinish();
    object->value = gl_timestamp();
    object->ready = TRUE;
}

static void begin_host(GlContext *ctx, GlQuery *q, int slot, GLuint stream, GLuint name)
{
    if (!q->mem.ptr && !gl_heap_alloc(256, &q->mem)) {
        gl_error(ctx, GL_OUT_OF_MEMORY);
        return;
    }
    if (!q->id)
        q->id = gl_new_id(ID_QUERY);
    q->host = TRUE;
    q->stream = stream;
    q->ready = FALSE;
    q->pending = FALSE;
    ((volatile uint64_t *)q->mem.ptr)[1] = 0;
    gl_device_lock();
    MimeGpuQuery *c = gl_emit(MIME_GPU_QUERY_BEGIN, sizeof(MimeGpuQuery));
    c->id = q->id;
    c->type = q->type;
    c->stream = stream;
    gl_device_unlock();
    ctx->st.active_query[slot][stream] = name;
}

static void begin_query(GlContext *ctx, GLenum target, GLuint index, GLuint name)
{
    uint32_t type;
    int slot = slot_of(target, &type);
    if (target == GL_TIME_ELAPSED) {
        if (index || ctx->elapsed_query || !name) {
            gl_error(ctx, !name || index ? GL_INVALID_OPERATION : GL_INVALID_OPERATION);
            return;
        }
        GlQuery *object = query(ctx, name, target);
        if (!object)
            return;
        glFinish();
        object->value = gl_timestamp();
        object->ready = FALSE;
        ctx->elapsed_query = name;
        return;
    }
    if (!host_target_ok(target)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GLuint streams = type >= MIME_GPU_QUERY_PRIMITIVES ? 1 : 1;
    if (index >= streams) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!name || ctx->st.active_query[slot][index]) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    GlQuery *q = query(ctx, name, target);
    if (!q)
        return;
    q->type = type;
    begin_host(ctx, q, slot, index, name);
}

GLAPI void APIENTRY glBeginQuery(GLenum target, GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    begin_query(ctx, target, 0, name);
}

GLAPI void APIENTRY glBeginQueryIndexed(GLenum target, GLuint index, GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    begin_query(ctx, target, index, name);
}

static void end_query(GlContext *ctx, GLenum target, GLuint index)
{
    uint32_t type;
    int slot = slot_of(target, &type);
    if (target == GL_TIME_ELAPSED) {
        if (!ctx->elapsed_query) {
            gl_error(ctx, GL_INVALID_OPERATION);
            return;
        }
        GlQuery *object = query(ctx, ctx->elapsed_query, target);
        glFinish();
        object->value = gl_timestamp() - object->value;
        object->ready = TRUE;
        ctx->elapsed_query = 0;
        return;
    }
    if (!host_target_ok(target)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (index >= 1) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    GLuint name = ctx->st.active_query[slot][index];
    GlQuery *q = name ? gl_name_get(&ctx->queries, name) : nullptr;
    if (!q || q == GL_RESERVED) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    gl_device_lock();
    /* clears held back by a pass the query is counting for must reach the host first. */
    MimeGpuQuery *c = gl_emit(MIME_GPU_QUERY_END, sizeof(MimeGpuQuery));
    c->id = q->id;
    c->type = q->type;
    c->result = gl_mem(&q->mem);
    gl_device_unlock();
    q->pending = TRUE;
    ctx->st.active_query[slot][index] = 0;
}

GLAPI void APIENTRY glEndQuery(GLenum target)
{
    GL_CONTEXT_OR_RETURN();
    end_query(ctx, target, 0);
}

GLAPI void APIENTRY glEndQueryIndexed(GLenum target, GLuint index)
{
    GL_CONTEXT_OR_RETURN();
    end_query(ctx, target, index);
}

static void query_iv(GlContext *ctx, GLenum target, GLuint index, GLenum pname, GLint *value)
{
    if (!value)
        return;
    uint32_t type;
    int slot = slot_of(target, &type);
    if (target != GL_TIMESTAMP && target != GL_TIME_ELAPSED && !host_target_ok(target)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (index >= 1) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (pname == GL_QUERY_COUNTER_BITS)
        *value = target == GL_SAMPLES_PASSED || target == GL_ANY_SAMPLES_PASSED ||
                         target == GL_ANY_SAMPLES_PASSED_CONSERVATIVE
                     ? 32
                 : slot >= 0 ? 64
                             : 64;
    else if (pname == GL_CURRENT_QUERY && target == GL_TIME_ELAPSED)
        *value = ctx->elapsed_query;
    else if (pname == GL_CURRENT_QUERY && slot >= 0)
        *value = (GLint)ctx->st.active_query[slot][index];
    else if (pname == GL_CURRENT_QUERY)
        *value = 0;
    else
        gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glGetQueryiv(GLenum target, GLenum pname, GLint *value)
{
    GL_CONTEXT_OR_RETURN();
    query_iv(ctx, target, 0, pname, value);
}

GLAPI void APIENTRY glGetQueryIndexediv(GLenum target, GLuint index, GLenum pname, GLint *value)
{
    GL_CONTEXT_OR_RETURN();
    query_iv(ctx, target, index, pname, value);
}

/* looks at the flag the host sets, waiting for it when the caller asks for the result. */
static BOOL host_ready(GlQuery *q, BOOL wait)
{
    volatile uint64_t *r = (volatile uint64_t *)q->mem.ptr;
    if (!r)
        return TRUE;
    if (__atomic_load_n(&r[1], __ATOMIC_ACQUIRE))
        return TRUE;
    if (!wait) {
        gl_device_lock();
        gl_publish();
        gl_device_unlock();
        return FALSE;
    }
    gl_wait(gl_fence());
    return __atomic_load_n(&r[1], __ATOMIC_ACQUIRE) != 0 || GlDev.hdr->lost;
}

static BOOL query_value(GlContext *ctx, GlQuery *q, GLenum pname, GLuint64 *value)
{
    if (pname == GL_QUERY_RESULT_AVAILABLE || pname == GL_QUERY_RESULT_NO_WAIT) {
        BOOL ready = q->host ? (q->ready || host_ready(q, FALSE)) : q->ready;
        if (pname == GL_QUERY_RESULT_AVAILABLE) {
            *value = ready;
            return TRUE;
        }
        if (!ready) {
            return FALSE;
        }
    } else if (pname != GL_QUERY_RESULT) {
        gl_error(ctx, GL_INVALID_ENUM);
        return FALSE;
    }
    if (q->host && !q->ready) {
        if (!host_ready(q, TRUE)) {
            *value = 0;
            return TRUE;
        }
        q->value = ((volatile uint64_t *)q->mem.ptr)[0];
        q->ready = TRUE;
        q->pending = FALSE;
    }
    *value = q->value;
    return TRUE;
}

static BOOL result(GlContext *ctx, GLuint name, GLenum pname, GLuint64 *value)
{
    GlQuery *object = gl_name_get(&ctx->queries, name);
    BOOL running = object && object != GL_RESERVED && object->host && !object->pending && !object->ready;
    if (!object || (void *)object == GL_RESERVED || ctx->elapsed_query == name || running || !object->target) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return FALSE;
    }
    return query_value(ctx, object, pname, value);
}

GLAPI void APIENTRY glGetQueryObjectui64v(GLuint name, GLenum pname, GLuint64 *value)
{
    GL_CONTEXT_OR_RETURN();
    if (value)
        result(ctx, name, pname, value);
}

GLAPI void APIENTRY glGetQueryObjecti64v(GLuint name, GLenum pname, GLint64 *value)
{
    GL_CONTEXT_OR_RETURN();
    GLuint64 number;
    if (value && result(ctx, name, pname, &number))
        *value = (GLint64)number;
}

GLAPI void APIENTRY glGetQueryObjectuiv(GLuint name, GLenum pname, GLuint *value)
{
    GL_CONTEXT_OR_RETURN();
    GLuint64 number;
    if (value && result(ctx, name, pname, &number))
        *value = number > 0xffffffffu ? 0xffffffffu : (GLuint)number;
}

GLAPI void APIENTRY glGetQueryObjectiv(GLuint name, GLenum pname, GLint *value)
{
    GL_CONTEXT_OR_RETURN();
    GLuint64 number;
    if (value && result(ctx, name, pname, &number))
        *value = number > 0x7fffffffu ? 0x7fffffff : (GLint)number;
}

/* ---------------------------------------------------------------- conditional rendering */

GLAPI void APIENTRY glBeginConditionalRender(GLuint name, GLenum mode)
{
    GL_CONTEXT_OR_RETURN();
    BOOL inverted = mode >= GL_QUERY_WAIT_INVERTED && mode <= GL_QUERY_BY_REGION_NO_WAIT_INVERTED;
    BOOL no_wait = mode == GL_QUERY_NO_WAIT || mode == GL_QUERY_BY_REGION_NO_WAIT ||
                   mode == GL_QUERY_NO_WAIT_INVERTED || mode == GL_QUERY_BY_REGION_NO_WAIT_INVERTED;
    if (!(mode >= GL_QUERY_WAIT && mode <= GL_QUERY_BY_REGION_NO_WAIT) && !inverted) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GlQuery *q = gl_name_get(&ctx->queries, name);
    if (ctx->st.condition || !q || q == GL_RESERVED || !q->host || q->type > MIME_GPU_QUERY_ANY_SAMPLES ||
        (!q->pending && !q->ready)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    GLuint64 value = 1;
    BOOL known = TRUE;
    if (no_wait && !q->ready && !host_ready(q, FALSE))
        known = FALSE;
    else if (!query_value(ctx, q, GL_QUERY_RESULT, &value))
        known = FALSE;
    ctx->st.condition = name;
    ctx->st.condition_mode = mode;
    /* a result that is not there yet, asked for without waiting, lets the drawing happen. */
    ctx->st.skip = known && ((value == 0) != inverted);
}

GLAPI void APIENTRY glEndConditionalRender(void)
{
    GL_CONTEXT_OR_RETURN();
    if (!ctx->st.condition) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    ctx->st.condition = 0;
    ctx->st.skip = FALSE;
}
