/*
 * the stages after the vertex shader that OpenGL 3.2 and 4.0 added: geometry and tessellation shaders, patches, the
 * viewport arrays and rasterizer discard. a program's extra sources travel to the host in front of its create command,
 * the host reports what the shaders declare, and the limits below are the ones of the host's device.
 */
#include "opengl32.h"

#define MAX_PATCH_VERTICES 32

BOOL gl_draw_mode_valid(GlContext *ctx, GLenum mode)
{
    if (mode <= GL_POLYGON)
        return TRUE;
    if (mode >= GL_LINES_ADJACENCY && mode <= GL_TRIANGLE_STRIP_ADJACENCY)
        return gl_host_has(MIME_GPU_FEATURE_GEOMETRY);
    return mode == GL_PATCHES && gl_host_has(MIME_GPU_FEATURE_TESSELLATION);
}

enum { STAGE_CONTROL = 1 << 2, STAGE_EVALUATION = 1 << 3, STAGE_GEOMETRY = 1 << 4 };

static BOOL geometry_takes(GLenum input, GLenum mode)
{
    switch (input) {
    case GL_POINTS:
        return mode == GL_POINTS;
    case GL_LINES:
        return mode == GL_LINES || mode == GL_LINE_STRIP || mode == GL_LINE_LOOP;
    case GL_LINES_ADJACENCY:
        return mode == GL_LINES_ADJACENCY || mode == GL_LINE_STRIP_ADJACENCY;
    case GL_TRIANGLES:
        return mode == GL_TRIANGLES || mode == GL_TRIANGLE_STRIP || mode == GL_TRIANGLE_FAN;
    case GL_TRIANGLES_ADJACENCY:
        return mode == GL_TRIANGLES_ADJACENCY || mode == GL_TRIANGLE_STRIP_ADJACENCY;
    default:
        return TRUE;
    }
}

BOOL gl_draw_mode_fits(GlContext *ctx, GlProgram *p, GLenum mode)
{
    uint32_t stages = p->stage.stages;
    BOOL tess = (stages & (STAGE_CONTROL | STAGE_EVALUATION)) == (STAGE_CONTROL | STAGE_EVALUATION);
    BOOL ok = TRUE;
    if (mode == GL_PATCHES)
        ok = tess;
    else if (tess)
        ok = FALSE;
    else if (mode >= GL_LINES_ADJACENCY && mode <= GL_TRIANGLE_STRIP_ADJACENCY && !(stages & STAGE_GEOMETRY))
        ok = FALSE;
    else if (stages & STAGE_GEOMETRY)
        ok = geometry_takes(p->stage.geometry_input, mode);
    if (!ok)
        gl_error(ctx, GL_INVALID_OPERATION);
    return ok;
}

/* the topology class a pipeline is made for: patches, or what a geometry shader takes. */
uint32_t gl_pipeline_topology(GlContext *ctx, const GlProgram *p)
{
    uint32_t stages = p->stage.stages;
    if ((stages & (STAGE_CONTROL | STAGE_EVALUATION)) == (STAGE_CONTROL | STAGE_EVALUATION)) {
        GLint n = ctx->st.patch_vertices ? ctx->st.patch_vertices : 3;
        return 4 | (uint32_t)n << 8;
    }
    return 0;
}

/* the host's primitive for a draw mode: Metal's numbers up to 4, Vulkan's topologies for adjacency and patches. */
uint32_t gl_primitive_of(GLenum mode)
{
    if (mode >= GL_LINES_ADJACENCY && mode <= GL_TRIANGLE_STRIP_ADJACENCY)
        return mode - 4;
    return mode == GL_PATCHES ? 10 : 3;
}

/* ---------------------------------------------------------------- patches */

GLAPI void APIENTRY glPatchParameteri(GLenum pname, GLint value)
{
    GL_CONTEXT_OR_RETURN();
    if (pname != GL_PATCH_VERTICES || !gl_host_has(MIME_GPU_FEATURE_TESSELLATION)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (value <= 0 || value > MAX_PATCH_VERTICES) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    ctx->st.patch_vertices = value;
    ctx->dirty |= DIRTY_PIPELINE;
}

GLAPI void APIENTRY glPatchParameterfv(GLenum pname, const GLfloat *values)
{
    GL_CONTEXT_OR_RETURN();
    if (pname == GL_PATCH_DEFAULT_OUTER_LEVEL && values) {
        memcpy(ctx->st.patch_outer, values, sizeof(ctx->st.patch_outer));
    } else if (pname == GL_PATCH_DEFAULT_INNER_LEVEL && values) {
        memcpy(ctx->st.patch_inner, values, sizeof(ctx->st.patch_inner));
    } else {
        gl_error(ctx, GL_INVALID_ENUM);
    }
}

/* ---------------------------------------------------------------- programs */

GLAPI void APIENTRY glProgramParameteri(GLuint program, GLenum pname, GLint value)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, program);
    if (!p) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    switch (pname) {
    case GL_PROGRAM_BINARY_RETRIEVABLE_HINT:
        break;
    case GL_PROGRAM_SEPARABLE:
        if (value != GL_TRUE && value != GL_FALSE) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
        p->separable = value == GL_TRUE;
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        break;
    }
}

BOOL gl_program_stages_ok(GlProgram *p, const char *control, const char *evaluation, const char *geometry)
{
    const char *why = nullptr;
    if ((control || evaluation) && !gl_host_has(MIME_GPU_FEATURE_TESSELLATION))
        why = "tessellation shaders are not supported by the gpu\n";
    else if (geometry && !gl_host_has(MIME_GPU_FEATURE_GEOMETRY))
        why = "geometry shaders are not supported by the gpu\n";
    else if (!control != !evaluation)
        why = "tessellation needs a control and an evaluation shader\n";
    else if (p->feedback_name_count && !gl_host_has(MIME_GPU_FEATURE_FEEDBACK))
        why = "transform feedback is not supported by the gpu\n";
    if (!why)
        return TRUE;
    gl_program_ready(p);
    p->linked = TRUE;
    p->link_status = FALSE;
    gl_free(p->log);
    p->log = gl_strdup(why);
    return FALSE;
}

/* the sources a link will send, kept until the next one. */
void gl_program_set_stage_sources(GlProgram *p, const char *control, const char *evaluation, const char *geometry)
{
    const char *src[3] = {control, evaluation, geometry};
    for (int x = 0; x < 3; x++) {
        gl_free(p->stage_source[x]);
        p->stage_source[x] = src[x] ? gl_strdup(src[x]) : nullptr;
    }
}

void gl_program_send_stages(GlProgram *p)
{
    SIZE_T sizes[3] = {};
    SIZE_T names = 0;
    for (int x = 0; x < 3; x++)
        sizes[x] = p->stage_source[x] ? strlen(p->stage_source[x]) : 0;
    for (uint32_t i = 0; i < p->feedback_name_count; i++)
        names += strlen(p->feedback_names[i]) + 1;
    if (!sizes[0] && !sizes[1] && !sizes[2] && !names && !p->frag_bindings_len)
        return;
    MimeGpuProgramStages *c = gl_emit(MIME_GPU_PROGRAM_STAGES, (uint32_t)(sizeof(MimeGpuProgramStages) + sizes[0] + sizes[1] +
                                                                           sizes[2] + names + p->frag_bindings_len));
    c->fragment_bytes = p->frag_bindings_len;
    c->id = p->id;
    c->control_bytes = (uint32_t)sizes[0];
    c->evaluation_bytes = (uint32_t)sizes[1];
    c->geometry_bytes = (uint32_t)sizes[2];
    c->feedback_bytes = (uint32_t)names;
    c->feedback_separate = p->feedback_mode == GL_SEPARATE_ATTRIBS;
    char *body = (char *)(c + 1);
    for (int x = 0; x < 3; x++) {
        if (sizes[x])
            memcpy(body, p->stage_source[x], sizes[x]);
        body += sizes[x];
    }
    for (uint32_t i = 0; i < p->feedback_name_count; i++) {
        SIZE_T n = strlen(p->feedback_names[i]) + 1;
        memcpy(body, p->feedback_names[i], n);
        body += n;
    }
    if (p->frag_bindings_len)
        memcpy(body, p->frag_bindings, p->frag_bindings_len);
}

void gl_program_free_stages(GlProgram *p)
{
    for (uint32_t i = 0; i < p->feedback_count; i++)
        gl_free(p->feedback[i].name);
    gl_free(p->feedback);
    p->feedback = nullptr;
    p->feedback_count = 0;
    memset(&p->stage, 0, sizeof(p->stage));
}

void gl_program_take_stages(GlProgram *p, const uint8_t *base, const MimeGpuProgramInfo *info)
{
    p->stage = info->stage;
    const MimeGpuFeedbackInfo *f = (const MimeGpuFeedbackInfo *)(base + info->stage.feedback_offset);
    p->feedback = gl_alloc((info->stage.feedback_count + 1) * sizeof(*p->feedback));
    for (uint32_t i = 0; p->feedback && i < info->stage.feedback_count; i++) {
        p->feedback[i].name = gl_strdup((const char *)base + f[i].name_offset);
        p->feedback[i].type = f[i].gl_type;
        p->feedback[i].size = (GLint)f[i].size;
        p->feedback[i].buffer = f[i].buffer;
        p->feedback_count++;
    }
}

/* the queries of glGetProgramiv that belong to these stages. */
BOOL gl_program_query_stage(GlProgram *p, GLenum pname, GLint *value)
{
    const MimeGpuStageInfo *s = &p->stage;
    switch (pname) {
    case GL_GEOMETRY_VERTICES_OUT:
        *value = (GLint)s->geometry_vertices;
        return TRUE;
    case GL_GEOMETRY_INPUT_TYPE:
        *value = (GLint)s->geometry_input;
        return TRUE;
    case GL_GEOMETRY_OUTPUT_TYPE:
        *value = (GLint)s->geometry_output;
        return TRUE;
    case GL_GEOMETRY_SHADER_INVOCATIONS:
        *value = (GLint)(s->geometry_invocations ? s->geometry_invocations : 1);
        return TRUE;
    case GL_TESS_CONTROL_OUTPUT_VERTICES:
        *value = (GLint)s->control_vertices;
        return TRUE;
    case GL_TESS_GEN_MODE:
        *value = (GLint)s->evaluation_mode;
        return TRUE;
    case GL_TESS_GEN_SPACING:
        *value = (GLint)(s->evaluation_spacing ? s->evaluation_spacing : GL_EQUAL);
        return TRUE;
    case GL_TESS_GEN_VERTEX_ORDER:
        *value = s->evaluation_ccw ? GL_CCW : GL_CW;
        return TRUE;
    case GL_TESS_GEN_POINT_MODE:
        *value = s->evaluation_point_mode ? GL_TRUE : GL_FALSE;
        return TRUE;
    case GL_TRANSFORM_FEEDBACK_BUFFER_MODE:
        *value = (GLint)(p->feedback_mode ? p->feedback_mode : GL_INTERLEAVED_ATTRIBS);
        return TRUE;
    case GL_TRANSFORM_FEEDBACK_VARYINGS:
        *value = (GLint)p->feedback_count;
        return TRUE;
    case GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH: {
        GLint longest = 0;
        for (uint32_t i = 0; i < p->feedback_count; i++) {
            GLint n = (GLint)strlen(p->feedback[i].name) + 1;
            longest = n > longest ? n : longest;
        }
        *value = longest;
        return TRUE;
    }
    default:
        return FALSE;
    }
}

/* ---------------------------------------------------------------- limits */

int gl_stage_get(GlContext *ctx, GLenum pname, GLdouble *v)
{
    GlStages *st = &ctx->st;
    switch (pname) {
    case GL_PATCH_VERTICES:
        v[0] = st->patch_vertices ? st->patch_vertices : 3;
        return 1;
    case GL_PATCH_DEFAULT_OUTER_LEVEL:
        for (int i = 0; i < 4; i++)
            v[i] = st->patch_outer[i];
        return 4;
    case GL_PATCH_DEFAULT_INNER_LEVEL:
        v[0] = st->patch_inner[0];
        v[1] = st->patch_inner[1];
        return 2;
    case GL_MAX_PATCH_VERTICES:
        v[0] = MAX_PATCH_VERTICES;
        return 1;
    case GL_MAX_TESS_GEN_LEVEL:
        v[0] = 64;
        return 1;
    case GL_MAX_TESS_CONTROL_UNIFORM_COMPONENTS:
    case GL_MAX_TESS_EVALUATION_UNIFORM_COMPONENTS:
    case GL_MAX_GEOMETRY_UNIFORM_COMPONENTS:
        v[0] = 1024;
        return 1;
    case GL_MAX_TESS_CONTROL_TEXTURE_IMAGE_UNITS:
    case GL_MAX_TESS_EVALUATION_TEXTURE_IMAGE_UNITS:
    case GL_MAX_GEOMETRY_TEXTURE_IMAGE_UNITS:
        v[0] = 16;
        return 1;
    case GL_MAX_TESS_CONTROL_OUTPUT_COMPONENTS:
    case GL_MAX_TESS_EVALUATION_OUTPUT_COMPONENTS:
    case GL_MAX_TESS_CONTROL_INPUT_COMPONENTS:
    case GL_MAX_TESS_EVALUATION_INPUT_COMPONENTS:
    case GL_MAX_GEOMETRY_INPUT_COMPONENTS:
    case GL_MAX_GEOMETRY_OUTPUT_COMPONENTS:
    case GL_MAX_VERTEX_OUTPUT_COMPONENTS:
    case GL_MAX_FRAGMENT_INPUT_COMPONENTS:
        v[0] = 128;
        return 1;
    case GL_MAX_TESS_PATCH_COMPONENTS:
        v[0] = 120;
        return 1;
    case GL_MAX_TESS_CONTROL_TOTAL_OUTPUT_COMPONENTS:
        v[0] = 4096;
        return 1;
    case GL_MAX_TESS_CONTROL_UNIFORM_BLOCKS:
    case GL_MAX_TESS_EVALUATION_UNIFORM_BLOCKS:
    case GL_MAX_GEOMETRY_UNIFORM_BLOCKS:
        v[0] = MIME_GPU_MAX_UNIFORM_BLOCKS;
        return 1;
    case GL_MAX_COMBINED_TESS_CONTROL_UNIFORM_COMPONENTS:
    case GL_MAX_COMBINED_TESS_EVALUATION_UNIFORM_COMPONENTS:
    case GL_MAX_COMBINED_GEOMETRY_UNIFORM_COMPONENTS:
        v[0] = 1024 + MIME_GPU_MAX_UNIFORM_BLOCKS * 4096;
        return 1;
    case GL_MAX_GEOMETRY_OUTPUT_VERTICES:
        v[0] = 256;
        return 1;
    case GL_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS:
        v[0] = 1024;
        return 1;
    case GL_MAX_GEOMETRY_SHADER_INVOCATIONS:
        v[0] = 32;
        return 1;
    case GL_MAX_VERTEX_STREAMS:
        v[0] = 1;
        return 1;
    case GL_MAX_TRANSFORM_FEEDBACK_BUFFERS:
    case GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS:
        v[0] = MIME_GPU_MAX_XFB_BUFFERS;
        return 1;
    case GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS:
    case GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS:
        v[0] = 128;
        return 1;
    case GL_TRANSFORM_FEEDBACK_BUFFER_BINDING:
        v[0] = st->xfb_buffer;
        return 1;
    case GL_TRANSFORM_FEEDBACK_BINDING:
        v[0] = st->xfb ? st->xfb->name : 0;
        return 1;
    case GL_TRANSFORM_FEEDBACK_ACTIVE:
        v[0] = st->xfb && st->xfb->active;
        return 1;
    case GL_TRANSFORM_FEEDBACK_PAUSED:
        v[0] = st->xfb && st->xfb->paused;
        return 1;
    case GL_MAX_VIEWPORTS:
        v[0] = gl_host_has(MIME_GPU_FEATURE_VIEWPORTS) ? MIME_GPU_MAX_VIEWPORTS : 1;
        return 1;
    case GL_MIN_PROGRAM_TEXTURE_GATHER_OFFSET:
        v[0] = -8;
        return 1;
    case GL_MAX_PROGRAM_TEXTURE_GATHER_OFFSET:
        v[0] = 7;
        return 1;
    case GL_MAX_PROGRAM_TEXTURE_GATHER_COMPONENTS_ARB:
        v[0] = 4;
        return 1;
    case GL_MAX_DUAL_SOURCE_DRAW_BUFFERS:
        v[0] = 1;
        return 1;
    case GL_VIEWPORT_SUBPIXEL_BITS:
        v[0] = 8;
        return 1;
    case GL_VIEWPORT_BOUNDS_RANGE:
        v[0] = -32768;
        v[1] = 32767;
        return 2;
    case GL_LAYER_PROVOKING_VERTEX:
    case GL_VIEWPORT_INDEX_PROVOKING_VERTEX:
        v[0] = GL_LAST_VERTEX_CONVENTION;
        return 1;
    default:
        return 0;
    }
}

/* ---------------------------------------------------------------- viewport arrays */

static BOOL viewport_index_ok(GlContext *ctx, GLuint first, GLsizei count)
{
    GLuint most = gl_host_has(MIME_GPU_FEATURE_VIEWPORTS) ? MIME_GPU_MAX_VIEWPORTS : 1;
    if (count < 0 || first > most || (GLuint)count > most - first) {
        gl_error(ctx, GL_INVALID_VALUE);
        return FALSE;
    }
    return TRUE;
}

/* an index that differs from the plain viewport starts from it. */
static void own_viewport(GlContext *ctx, GLuint i)
{
    if (ctx->st.viewport_mask & (1u << i))
        return;
    for (int k = 0; k < 4; k++)
        ctx->st.viewport[i][k] = (GLfloat)ctx->viewport[k];
    ctx->st.depth_range[i][0] = ctx->depth_range[0];
    ctx->st.depth_range[i][1] = ctx->depth_range[1];
    ctx->st.viewport_mask |= 1u << i;
}

static void set_viewport(GlContext *ctx, GLuint i, GLfloat x, GLfloat y, GLfloat w, GLfloat h)
{
    if (i == 0) {
        ctx->viewport[0] = (GLint)x;
        ctx->viewport[1] = (GLint)y;
        ctx->viewport[2] = (GLint)w;
        ctx->viewport[3] = (GLint)h;
    } else {
        own_viewport(ctx, i);
        GLfloat *v = ctx->st.viewport[i];
        v[0] = x;
        v[1] = y;
        v[2] = w;
        v[3] = h;
    }
    ctx->dirty |= DIRTY_VIEWPORT;
}

/* glViewport, glScissor and glDepthRange set every index: the ones that differ take the new values. */
void gl_stage_viewport_changed(GlContext *ctx)
{
    for (uint32_t i = 1; i < MIME_GPU_MAX_VIEWPORTS; i++)
        if (ctx->st.viewport_mask & (1u << i))
            for (int k = 0; k < 4; k++)
                ctx->st.viewport[i][k] = (GLfloat)ctx->viewport[k];
}

void gl_stage_scissor_changed(GlContext *ctx)
{
    for (uint32_t i = 1; i < MIME_GPU_MAX_VIEWPORTS; i++)
        if (ctx->st.scissor_mask & (1u << i))
            for (int k = 0; k < 4; k++)
                ctx->st.scissor[i][k] = ctx->scissor[k];
}

void gl_stage_depth_changed(GlContext *ctx)
{
    for (uint32_t i = 1; i < MIME_GPU_MAX_VIEWPORTS; i++)
        if (ctx->st.viewport_mask & (1u << i)) {
            ctx->st.depth_range[i][0] = ctx->depth_range[0];
            ctx->st.depth_range[i][1] = ctx->depth_range[1];
        }
}

GLAPI void APIENTRY glViewportArrayv(GLuint first, GLsizei count, const GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
    if (!viewport_index_ok(ctx, first, count) || !v)
        return;
    for (GLsizei i = 0; i < count; i++) {
        if (v[i * 4 + 2] < 0 || v[i * 4 + 3] < 0) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
    }
    for (GLsizei i = 0; i < count; i++)
        set_viewport(ctx, first + (GLuint)i, v[i * 4], v[i * 4 + 1], v[i * 4 + 2], v[i * 4 + 3]);
}

GLAPI void APIENTRY glViewportIndexedf(GLuint index, GLfloat x, GLfloat y, GLfloat w, GLfloat h)
{
    GL_CONTEXT_OR_RETURN();
    if (!viewport_index_ok(ctx, index, 1))
        return;
    if (w < 0 || h < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    set_viewport(ctx, index, x, y, w, h);
}

GLAPI void APIENTRY glViewportIndexedfv(GLuint index, const GLfloat *v)
{
    glViewportIndexedf(index, v[0], v[1], v[2], v[3]);
}

static void set_scissor(GlContext *ctx, GLuint i, GLint x, GLint y, GLint w, GLint h)
{
    if (i == 0) {
        ctx->scissor[0] = x;
        ctx->scissor[1] = y;
        ctx->scissor[2] = w;
        ctx->scissor[3] = h;
    } else {
        if (!(ctx->st.scissor_mask & (1u << i)))
            ctx->st.scissor_mask |= 1u << i;
        GLint *v = ctx->st.scissor[i];
        v[0] = x;
        v[1] = y;
        v[2] = w;
        v[3] = h;
    }
    ctx->dirty |= DIRTY_SCISSOR;
}

GLAPI void APIENTRY glScissorArrayv(GLuint first, GLsizei count, const GLint *v)
{
    GL_CONTEXT_OR_RETURN();
    if (!viewport_index_ok(ctx, first, count) || !v)
        return;
    for (GLsizei i = 0; i < count; i++) {
        if (v[i * 4 + 2] < 0 || v[i * 4 + 3] < 0) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
    }
    for (GLsizei i = 0; i < count; i++)
        set_scissor(ctx, first + (GLuint)i, v[i * 4], v[i * 4 + 1], v[i * 4 + 2], v[i * 4 + 3]);
}

GLAPI void APIENTRY glScissorIndexed(GLuint index, GLint left, GLint bottom, GLsizei width, GLsizei height)
{
    GL_CONTEXT_OR_RETURN();
    if (!viewport_index_ok(ctx, index, 1))
        return;
    if (width < 0 || height < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    set_scissor(ctx, index, left, bottom, width, height);
}

GLAPI void APIENTRY glScissorIndexedv(GLuint index, const GLint *v)
{
    glScissorIndexed(index, v[0], v[1], v[2], v[3]);
}

static void set_depth_range(GlContext *ctx, GLuint i, GLdouble n, GLdouble f)
{
    n = n < 0 ? 0 : n > 1 ? 1 : n;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    if (i == 0) {
        ctx->depth_range[0] = n;
        ctx->depth_range[1] = f;
    } else {
        own_viewport(ctx, i);
        ctx->st.depth_range[i][0] = n;
        ctx->st.depth_range[i][1] = f;
    }
    ctx->dirty |= DIRTY_VIEWPORT;
}

GLAPI void APIENTRY glDepthRangeArrayv(GLuint first, GLsizei count, const GLdouble *v)
{
    GL_CONTEXT_OR_RETURN();
    if (!viewport_index_ok(ctx, first, count) || !v)
        return;
    for (GLsizei i = 0; i < count; i++)
        set_depth_range(ctx, first + (GLuint)i, v[i * 2], v[i * 2 + 1]);
}

GLAPI void APIENTRY glDepthRangeIndexed(GLuint index, GLdouble n, GLdouble f)
{
    GL_CONTEXT_OR_RETURN();
    if (!viewport_index_ok(ctx, index, 1))
        return;
    set_depth_range(ctx, index, n, f);
}

/* an indexed query of the viewports, scissors and depth ranges. */
static int indexed_values(GlContext *ctx, GLenum target, GLuint index, GLdouble *v)
{
    if (index >= (gl_host_has(MIME_GPU_FEATURE_VIEWPORTS) ? MIME_GPU_MAX_VIEWPORTS : 1u)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return 0;
    }
    BOOL view = index && (ctx->st.viewport_mask & (1u << index));
    switch (target) {
    case GL_VIEWPORT:
        for (int i = 0; i < 4; i++)
            v[i] = view ? ctx->st.viewport[index][i] : (GLdouble)ctx->viewport[i];
        return 4;
    case GL_SCISSOR_BOX:
        for (int i = 0; i < 4; i++)
            v[i] = index && (ctx->st.scissor_mask & (1u << index)) ? ctx->st.scissor[index][i] : ctx->scissor[i];
        return 4;
    case GL_DEPTH_RANGE:
        v[0] = view ? ctx->st.depth_range[index][0] : ctx->depth_range[0];
        v[1] = view ? ctx->st.depth_range[index][1] : ctx->depth_range[1];
        return 2;
    case GL_SCISSOR_TEST:
        v[0] = ctx->scissor_test;
        return 1;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        return 0;
    }
}

GLAPI void APIENTRY glGetFloati_v(GLenum target, GLuint index, GLfloat *data)
{
    GL_CONTEXT_OR_RETURN();
    GLdouble v[4];
    int n = indexed_values(ctx, target, index, v);
    for (int i = 0; i < n; i++)
        data[i] = (GLfloat)v[i];
}

GLAPI void APIENTRY glGetDoublei_v(GLenum target, GLuint index, GLdouble *data)
{
    GL_CONTEXT_OR_RETURN();
    GLdouble v[4];
    int n = indexed_values(ctx, target, index, v);
    for (int i = 0; i < n; i++)
        data[i] = v[i];
}

/* sends the viewports and scissors that differ from the plain ones, once the plain ones went. */
void gl_stage_emit_viewports(GlContext *ctx, uint32_t dirty)
{
    GlStages *st = &ctx->st;
    if (!gl_host_has(MIME_GPU_FEATURE_VIEWPORTS))
        return;
    if (dirty & DIRTY_VIEWPORT) {
        for (uint32_t i = 1; i < MIME_GPU_MAX_VIEWPORTS; i++) {
            if (!(st->viewport_mask & (1u << i)))
                continue;
            MimeGpuSetViewportIndexed *v = gl_emit(MIME_GPU_SET_VIEWPORT_INDEXED, sizeof(MimeGpuSetViewportIndexed));
            v->index = i;
            v->x = st->viewport[i][0];
            v->y = st->viewport[i][1];
            v->width = st->viewport[i][2];
            v->height = st->viewport[i][3];
            v->znear = (float)st->depth_range[i][0];
            v->zfar = (float)st->depth_range[i][1];
        }
    }
    if ((dirty & DIRTY_SCISSOR) && ctx->scissor_test) {
        for (uint32_t i = 1; i < MIME_GPU_MAX_VIEWPORTS; i++) {
            if (!(st->scissor_mask & (1u << i)))
                continue;
            MimeGpuSetScissorIndexed *s = gl_emit(MIME_GPU_SET_SCISSOR_INDEXED, sizeof(MimeGpuSetScissorIndexed));
            s->index = i;
            s->x = st->scissor[i][0];
            s->y = st->scissor[i][1];
            s->width = (uint32_t)st->scissor[i][2];
            s->height = (uint32_t)st->scissor[i][3];
        }
    }
}
