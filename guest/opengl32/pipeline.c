/*
 * separate shader objects. a program linked as separable holds the stages it was made of, and a program pipeline picks
 * one program for each stage. the host links whole programs, so a pipeline is turned into one on its first draw: the
 * sources of its stage programs are linked together again, and again when one of them is relinked. the values of the
 * uniforms the stage programs hold are copied into that program by name before each draw that follows a change.
 */
#include "opengl32.h"

void APIENTRY glDeleteShader(GLuint name);
GLuint APIENTRY glCreateShader(GLenum type);
void APIENTRY glShaderSource(GLuint name, GLsizei count, const GLchar *const *strings, const GLint *lengths);
void APIENTRY glCompileShader(GLuint name);
GLuint APIENTRY glCreateProgram(void);
void APIENTRY glAttachShader(GLuint program, GLuint shader);
void APIENTRY glDetachShader(GLuint program, GLuint shader);
void APIENTRY glLinkProgram(GLuint program);
void APIENTRY glDeleteProgram(GLuint program);
void APIENTRY glProgramParameteri(GLuint program, GLenum pname, GLint value);

enum { PIPE_VERTEX, PIPE_CONTROL, PIPE_EVALUATION, PIPE_GEOMETRY, PIPE_FRAGMENT, PIPE_STAGES };

typedef struct GlPipeline {
    GLuint name;
    BOOL created;
    GLuint program[PIPE_STAGES];
    GLuint active;
    GlProgram *combined;
    uint32_t serial[PIPE_STAGES];  /* link serials the combined program was made from. */
    uint32_t version[PIPE_STAGES]; /* uniform versions last copied into it. */
    BOOL validated;
} GlPipeline;

static const GLbitfield g_stage_bit[PIPE_STAGES] = {GL_VERTEX_SHADER_BIT, GL_TESS_CONTROL_SHADER_BIT,
                                                    GL_TESS_EVALUATION_SHADER_BIT, GL_GEOMETRY_SHADER_BIT,
                                                    GL_FRAGMENT_SHADER_BIT};

static GlPipeline *pipeline_of(GlContext *ctx, GLuint name)
{
    void *p = gl_name_get(&ctx->st.pipelines, name);
    return p == GL_RESERVED ? nullptr : p;
}

/* a pipeline exists once it is bound, as a name from glGenProgramPipelines does not until then. */
static GlPipeline *pipeline_make(GlContext *ctx, GLuint name)
{
    void *p = gl_name_get(&ctx->st.pipelines, name);
    if (!p)
        return nullptr;
    if (p == GL_RESERVED) {
        GlPipeline *n = gl_alloc(sizeof(GlPipeline));
        if (!n)
            return nullptr;
        n->name = name;
        n->created = TRUE;
        gl_name_set(&ctx->st.pipelines, name, n);
        return n;
    }
    return p;
}

static void pipeline_free(GlPipeline *p)
{
    if (p->combined)
        gl_program_free_internal(p->combined);
    gl_free(p);
}

void gl_pipeline_context_destroy(GlContext *ctx)
{
    for (GLuint i = 0; i < ctx->st.pipelines.count; i++)
        if (ctx->st.pipelines.items[i] && ctx->st.pipelines.items[i] != GL_RESERVED)
            pipeline_free(ctx->st.pipelines.items[i]);
    gl_free(ctx->st.pipelines.items);
}

GLAPI void APIENTRY glGenProgramPipelines(GLsizei n, GLuint *pipelines)
{
    GL_CONTEXT_OR_RETURN();
    if (n < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < n; i++)
        pipelines[i] = gl_name_new(&ctx->st.pipelines);
}

GLAPI void APIENTRY glCreateProgramPipelines(GLsizei n, GLuint *pipelines)
{
    GL_CONTEXT_OR_RETURN();
    glGenProgramPipelines(n, pipelines);
    for (GLsizei i = 0; i < n && n > 0; i++)
        pipeline_make(ctx, pipelines[i]);
}

GLAPI void APIENTRY glDeleteProgramPipelines(GLsizei n, const GLuint *pipelines)
{
    GL_CONTEXT_OR_RETURN();
    if (n < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < n; i++) {
        void *o = pipelines[i] ? gl_name_get(&ctx->st.pipelines, pipelines[i]) : nullptr;
        if (!o)
            continue;
        if (o != GL_RESERVED) {
            if (ctx->st.pipeline == o) {
                ctx->st.pipeline = nullptr;
                ctx->st.pipeline_name = 0;
                ctx->dirty |= DIRTY_PIPELINE;
            }
            pipeline_free(o);
        }
        gl_name_set(&ctx->st.pipelines, pipelines[i], nullptr);
    }
}

GLAPI GLboolean APIENTRY glIsProgramPipeline(GLuint pipeline)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    return pipeline && pipeline_of(ctx, pipeline) != nullptr;
}

GLAPI void APIENTRY glBindProgramPipeline(GLuint pipeline)
{
    GL_CONTEXT_OR_RETURN();
    if (!pipeline) {
        ctx->st.pipeline = nullptr;
        ctx->st.pipeline_name = 0;
    } else {
        GlPipeline *p = pipeline_make(ctx, pipeline);
        if (!p) {
            gl_error(ctx, GL_INVALID_OPERATION);
            return;
        }
        ctx->st.pipeline = p;
        ctx->st.pipeline_name = pipeline;
    }
    ctx->dirty |= DIRTY_PIPELINE | DIRTY_UNIFORMS | DIRTY_TEXTURES;
}

GLAPI void APIENTRY glUseProgramStages(GLuint pipeline, GLbitfield stages, GLuint program)
{
    GL_CONTEXT_OR_RETURN();
    GlPipeline *pl = pipeline_make(ctx, pipeline);
    if (!pl) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (stages & ~(GLbitfield)GL_ALL_SHADER_BITS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    GlProgram *p = program ? gl_program(ctx, program) : nullptr;
    if (program && !p) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (p) {
        gl_program_ready(p);
        if (!p->separable || !p->link_status) {
            gl_error(ctx, GL_INVALID_OPERATION);
            return;
        }
    }
    for (int s = 0; s < PIPE_STAGES; s++) {
        if (!(stages & g_stage_bit[s]))
            continue;
        pl->program[s] = p && (p->stage_mask & (1u << s)) ? program : 0;
    }
    ctx->dirty |= DIRTY_PIPELINE | DIRTY_UNIFORMS | DIRTY_TEXTURES;
}

GLAPI void APIENTRY glActiveShaderProgram(GLuint pipeline, GLuint program)
{
    GL_CONTEXT_OR_RETURN();
    GlPipeline *pl = pipeline_make(ctx, pipeline);
    if (!pl || (program && !gl_program(ctx, program))) {
        gl_error(ctx, pl ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
        return;
    }
    pl->active = program;
}

GlProgram *gl_pipeline_active_program(GlContext *ctx)
{
    GlPipeline *pl = ctx->st.pipeline;
    return pl && pl->active ? gl_program(ctx, pl->active) : nullptr;
}

GLAPI GLuint APIENTRY glCreateShaderProgramv(GLenum type, GLsizei count, const GLchar *const *strings)
{
    GL_CONTEXT_OR_RETURN(0);
    if (count < 0 || (count && !strings)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return 0;
    }
    GLuint shader = glCreateShader(type);
    if (!shader)
        return 0;
    glShaderSource(shader, count, strings, nullptr);
    glCompileShader(shader);
    GLuint program = glCreateProgram();
    if (program) {
        glProgramParameteri(program, GL_PROGRAM_SEPARABLE, GL_TRUE);
        glAttachShader(program, shader);
        glLinkProgram(program);
        glDetachShader(program, shader);
    }
    glDeleteShader(shader);
    return program;
}

GLAPI void APIENTRY glGetProgramPipelineiv(GLuint pipeline, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GlPipeline *pl = pipeline_of(ctx, pipeline);
    if (!pl) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    switch (pname) {
    case GL_ACTIVE_PROGRAM:
        *params = (GLint)pl->active;
        break;
    case GL_VERTEX_SHADER:
        *params = (GLint)pl->program[PIPE_VERTEX];
        break;
    case GL_TESS_CONTROL_SHADER:
        *params = (GLint)pl->program[PIPE_CONTROL];
        break;
    case GL_TESS_EVALUATION_SHADER:
        *params = (GLint)pl->program[PIPE_EVALUATION];
        break;
    case GL_GEOMETRY_SHADER:
        *params = (GLint)pl->program[PIPE_GEOMETRY];
        break;
    case GL_FRAGMENT_SHADER:
        *params = (GLint)pl->program[PIPE_FRAGMENT];
        break;
    case GL_VALIDATE_STATUS:
        *params = pl->validated && pl->program[PIPE_VERTEX] && pl->program[PIPE_FRAGMENT];
        break;
    case GL_INFO_LOG_LENGTH:
        *params = 0;
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        break;
    }
}

GLAPI void APIENTRY glValidateProgramPipeline(GLuint pipeline)
{
    GL_CONTEXT_OR_RETURN();
    GlPipeline *pl = pipeline_of(ctx, pipeline);
    if (!pl) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    pl->validated = TRUE;
}

GLAPI void APIENTRY glGetProgramPipelineInfoLog(GLuint pipeline, GLsizei size, GLsizei *length, GLchar *log)
{
    if (length)
        *length = 0;
    if (size > 0 && log)
        log[0] = 0;
}

/* ---------------------------------------------------------------- drawing with a pipeline */

static const char g_dummy_vertex[] = "#version 330 core\nvoid main() { gl_Position = vec4(0.0); }\n";
static const char g_dummy_fragment[] = "#version 330 core\nvoid main() { }\n";

/* the program a draw uses when none is in use: the pipeline's stages linked into one, or null. */
GlProgram *gl_pipeline_resolve(GlContext *ctx)
{
    GlPipeline *pl = ctx->st.pipeline;
    if (!pl)
        return nullptr;
    GlProgram *stage[PIPE_STAGES] = {};
    BOOL changed = !pl->combined;
    for (int s = 0; s < PIPE_STAGES; s++) {
        if (!pl->program[s])
            continue;
        stage[s] = gl_program(ctx, pl->program[s]);
        if (stage[s] && !gl_program_ready(stage[s]))
            stage[s] = nullptr;
        uint32_t serial = stage[s] ? stage[s]->serial : 0;
        if (serial != pl->serial[s])
            changed = TRUE;
        pl->serial[s] = serial;
    }
    if (!stage[PIPE_VERTEX]) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return nullptr;
    }
    if (changed) {
        if (!pl->combined) {
            pl->combined = gl_alloc(sizeof(GlProgram));
            if (!pl->combined)
                return nullptr;
        }
        GlProgram *c = pl->combined;
        GlProgram *vs = stage[PIPE_VERTEX], *fs = stage[PIPE_FRAGMENT];
        gl_free(c->bindings);
        c->bindings = nullptr;
        c->bindings_len = 0;
        if (vs->bindings_len) {
            c->bindings = gl_alloc(vs->bindings_len);
            if (c->bindings) {
                memcpy(c->bindings, vs->bindings, vs->bindings_len);
                c->bindings_len = vs->bindings_len;
            }
        }
        gl_program_set_stage_sources(c, stage[PIPE_CONTROL] ? stage[PIPE_CONTROL]->stage_source[0] : nullptr,
                                     stage[PIPE_EVALUATION] ? stage[PIPE_EVALUATION]->stage_source[1] : nullptr,
                                     stage[PIPE_GEOMETRY] ? stage[PIPE_GEOMETRY]->stage_source[2] : nullptr);
        /* the varyings that transform feedback captures were named on the program of the last stage before the rasterizer. */
        GlProgram *named = nullptr;
        for (int s = PIPE_STAGES - 1; s >= 0 && !named; s--)
            if (stage[s] && stage[s]->feedback_name_count)
                named = stage[s];
        if (named)
            gl_program_set_feedback_names(c, (GLsizei)named->feedback_name_count, (const GLchar *const *)named->feedback_names,
                                          named->feedback_mode);
        else
            gl_program_set_feedback_names(c, 0, nullptr, 0);
        gl_program_link_sources(c, vs->main_source[0] ? vs->main_source[0] : g_dummy_vertex,
                                fs && fs->main_source[1] ? fs->main_source[1] : g_dummy_fragment);
        for (int s = 0; s < PIPE_STAGES; s++)
            pl->version[s] = 0;
    }
    if (!gl_program_ready(pl->combined))
        return nullptr;
    for (int s = 0; s < PIPE_STAGES; s++) {
        if (!stage[s] || stage[s]->uniform_version == pl->version[s])
            continue;
        gl_program_copy_uniforms(pl->combined, stage[s]);
        pl->version[s] = stage[s]->uniform_version;
        ctx->dirty |= DIRTY_UNIFORMS | DIRTY_TEXTURES;
    }
    return pl->combined;
}

GlProgram *gl_draw_program(GlContext *ctx)
{
    return ctx->current_program ? ctx->current_program : gl_pipeline_resolve(ctx);
}
