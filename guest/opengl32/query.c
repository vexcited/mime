/* synchronous timer queries use a monotonic clock after submitted gpu work finishes. */
#include "opengl32.h"

typedef struct GlQuery {
    GLenum target;
    GLuint64 value;
    BOOL ready;
} GlQuery;

GLuint64 gl_timestamp(void)
{
    LARGE_INTEGER counter, frequency;
    if (!NT_SUCCESS(NtQueryPerformanceCounter(&counter, &frequency)) || frequency.QuadPart <= 0)
        return 0;
    return (GLuint64)(counter.QuadPart / frequency.QuadPart) * 1000000000ull +
           (GLuint64)(counter.QuadPart % frequency.QuadPart) * 1000000000ull / frequency.QuadPart;
}

void gl_queries_destroy(GlContext *ctx)
{
    for (GLuint i = 0; i < ctx->queries.count; i++)
        if (ctx->queries.items[i] != GL_RESERVED)
            gl_free(ctx->queries.items[i]);
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
        if (object != GL_RESERVED)
            gl_free(object);
        gl_name_set(&ctx->queries, names[i], nullptr);
    }
}

GLAPI GLboolean APIENTRY glIsQuery(GLuint name)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    void *object = gl_name_get(&ctx->queries, name);
    return object && object != GL_RESERVED;
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

GLAPI void APIENTRY glBeginQuery(GLenum target, GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    if (target != GL_TIME_ELAPSED) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (ctx->elapsed_query) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    GlQuery *object = query(ctx, name, target);
    if (!object)
        return;
    glFinish();
    object->value = gl_timestamp();
    object->ready = FALSE;
    ctx->elapsed_query = name;
}

GLAPI void APIENTRY glEndQuery(GLenum target)
{
    GL_CONTEXT_OR_RETURN();
    if (target != GL_TIME_ELAPSED) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (!ctx->elapsed_query) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    GlQuery *object = query(ctx, ctx->elapsed_query, target);
    glFinish();
    object->value = gl_timestamp() - object->value;
    object->ready = TRUE;
    ctx->elapsed_query = 0;
}

GLAPI void APIENTRY glGetQueryiv(GLenum target, GLenum pname, GLint *value)
{
    GL_CONTEXT_OR_RETURN();
    if (!value)
        return;
    if (target != GL_TIMESTAMP && target != GL_TIME_ELAPSED) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (pname == GL_QUERY_COUNTER_BITS)
        *value = 64;
    else if (pname == GL_CURRENT_QUERY && target == GL_TIME_ELAPSED)
        *value = ctx->elapsed_query;
    else
        gl_error(ctx, GL_INVALID_ENUM);
}

static BOOL result(GlContext *ctx, GLuint name, GLenum pname, GLuint64 *value)
{
    GlQuery *object = gl_name_get(&ctx->queries, name);
    if (!object || (void *)object == GL_RESERVED || ctx->elapsed_query == name) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return FALSE;
    }
    if (pname == GL_QUERY_RESULT_AVAILABLE)
        *value = object->ready;
    else if (pname == GL_QUERY_RESULT)
        *value = object->value;
    else {
        gl_error(ctx, GL_INVALID_ENUM);
        return FALSE;
    }
    return TRUE;
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

/* a query object of the target for a name that has none, as glCreateQueries makes it. */
BOOL gl_query_create(GlContext *ctx, GLuint name, GLenum target)
{
    return query(ctx, name, target) != nullptr;
}
