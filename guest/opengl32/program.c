/*
 * shaders and programs. sources are kept until a link, when both stages go to the host compiler; the link is
 * asynchronous and only a query or a draw waits for it. uniforms live in a block laid out by the host's reflection,
 * written here and sent with the draws that need it. a location is the uniform's index times 4096 plus the array
 * element.
 */
#include "opengl32.h"

#include <mime/glcompat.h>

#define INFO_CAPACITY (128u << 10)

void shape_of(GLenum type, int *cols, int *rows, int *class);
enum { VALUES_INT = 0, VALUES_FLOAT = 1, VALUES_DOUBLE = 2 };
static void put_uniform(GlContext *ctx, GlProgram *p, GLint location, GLsizei count, int n, const void *values, int kind);

GlProgram *gl_program(GlContext *ctx, GLuint name)
{
    void *p = gl_name_get(&ctx->shared->programs, name);
    return p == GL_RESERVED ? nullptr : p;
}

static GlShader *shader(GlContext *ctx, GLuint name)
{
    void *s = gl_name_get(&ctx->shared->shaders, name);
    return s == GL_RESERVED ? nullptr : s;
}

/* programs and shaders share one name space, as GL requires. */
static GLuint new_name(GlContext *ctx)
{
    GLuint a = gl_name_new(&ctx->shared->programs);
    gl_name_set(&ctx->shared->shaders, a, GL_RESERVED);
    return a;
}

GLAPI GLuint APIENTRY glCreateShader(GLenum type)
{
    GL_CONTEXT_OR_RETURN(0);
    BOOL known = type == GL_VERTEX_SHADER || type == GL_FRAGMENT_SHADER ||
                 (type == GL_COMPUTE_SHADER && gl_host_has(MIME_GPU_FEATURE_COMPUTE)) ||
                 (type == GL_GEOMETRY_SHADER && gl_host_has(MIME_GPU_FEATURE_GEOMETRY)) ||
                 ((type == GL_TESS_CONTROL_SHADER || type == GL_TESS_EVALUATION_SHADER) &&
                  gl_host_has(MIME_GPU_FEATURE_TESSELLATION));
    if (!known) {
        gl_error(ctx, GL_INVALID_ENUM);
        return 0;
    }
    GlShader *s = gl_alloc(sizeof(GlShader));
    if (!s)
        return 0;
    GLuint name = new_name(ctx);
    s->name = name;
    s->type = type;
    s->refs = 1;
    gl_name_set(&ctx->shared->shaders, name, s);
    return name;
}

static void release_shader(GlContext *ctx, GlShader *s)
{
    if (--s->refs)
        return;
    gl_name_set(&ctx->shared->shaders, s->name, nullptr);
    gl_name_set(&ctx->shared->programs, s->name, nullptr);
    gl_free(s->source);
    gl_free(s);
}

GLAPI void APIENTRY glDeleteShader(GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    GlShader *s = shader(ctx, name);
    if (!s || s->deleted)
        return;
    s->deleted = TRUE;
    release_shader(ctx, s);
}

GLAPI GLboolean APIENTRY glIsShader(GLuint name)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    return shader(ctx, name) ? GL_TRUE : GL_FALSE;
}

GLAPI void APIENTRY glShaderSource(GLuint name, GLsizei count, const GLchar *const *strings, const GLint *lengths)
{
    GL_CONTEXT_OR_RETURN();
    GlShader *s = shader(ctx, name);
    if (!s) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    SIZE_T total = 0;
    for (GLsizei i = 0; i < count; i++)
        total += lengths && lengths[i] >= 0 ? (SIZE_T)lengths[i] : strlen(strings[i]);
    char *text = gl_alloc(total + 1);
    if (!text)
        return;
    SIZE_T at = 0;
    for (GLsizei i = 0; i < count; i++) {
        SIZE_T n = lengths && lengths[i] >= 0 ? (SIZE_T)lengths[i] : strlen(strings[i]);
        memcpy(text + at, strings[i], n);
        at += n;
    }
    gl_free(s->source);
    s->source = text;
}

GLAPI void APIENTRY glGetShaderSource(GLuint name, GLsizei size, GLsizei *length, GLchar *source)
{
    GL_CONTEXT_OR_RETURN();
    GlShader *s = shader(ctx, name);
    SIZE_T n = s && s->source ? strlen(s->source) : 0;
    if (size > 0) {
        if (n >= (SIZE_T)size)
            n = (SIZE_T)size - 1;
        if (n)
            memcpy(source, s->source, n);
        source[n] = 0;
    }
    if (length)
        *length = (GLsizei)n;
}

/* compiling waits for the link, where the host sees both stages. */
GLAPI void APIENTRY glCompileShader(GLuint name)
{
}

GLAPI void APIENTRY glGetShaderiv(GLuint name, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GlShader *s = shader(ctx, name);
    if (!s) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    switch (pname) {
    case GL_SHADER_TYPE:
        *params = (GLint)s->type;
        break;
    case GL_DELETE_STATUS:
        *params = s->deleted;
        break;
    case GL_COMPILE_STATUS:
        *params = GL_TRUE;
        break;
    case GL_INFO_LOG_LENGTH:
        *params = 0;
        break;
    case GL_SHADER_SOURCE_LENGTH:
        *params = s->source ? (GLint)strlen(s->source) + 1 : 0;
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        break;
    }
}

GLAPI void APIENTRY glGetShaderInfoLog(GLuint name, GLsizei size, GLsizei *length, GLchar *log)
{
    if (length)
        *length = 0;
    if (size > 0)
        log[0] = 0;
}

GLAPI void APIENTRY glGetShaderPrecisionFormat(GLenum shader_type, GLenum precision, GLint *range, GLint *prec)
{
    range[0] = range[1] = 127;
    *prec = 23;
}

GLAPI void APIENTRY glReleaseShaderCompiler(void)
{
}

/* ---------------------------------------------------------------- programs */

GLAPI GLuint APIENTRY glCreateProgram(void)
{
    GL_CONTEXT_OR_RETURN(0);
    GlProgram *p = gl_alloc(sizeof(GlProgram));
    if (!p)
        return 0;
    GLuint name = new_name(ctx);
    p->name = name;
    gl_name_set(&ctx->shared->programs, name, p);
    return name;
}

static void free_reflection(GlProgram *p)
{
    for (uint32_t i = 0; i < p->uniform_count; i++) {
        gl_free(p->uniforms[i].name);
        gl_free(p->uniforms[i].units);
    }
    for (uint32_t i = 0; i < p->attrib_count; i++)
        gl_free(p->attribs[i].name);
    for (uint32_t i = 0; i < p->block_count; i++)
        gl_free(p->blocks[i].name);
    gl_free(p->blocks);
    p->blocks = nullptr;
    p->block_count = 0;
    for (uint32_t i = 0; i < p->storage_count; i++)
        gl_free(p->storage[i].name);
    gl_free(p->storage);
    p->storage = nullptr;
    p->storage_count = 0;
    for (uint32_t i = 0; i < p->counter_count; i++)
        gl_free(p->counters[i].name);
    gl_free(p->counters);
    gl_free(p->counter_buffers);
    p->counters = nullptr;
    p->counter_buffers = nullptr;
    p->counter_count = p->counter_buffer_count = 0;
    gl_free(p->uniforms);
    gl_free(p->attribs);
    gl_free(p->ublock);
    gl_free(p->log);
    gl_program_free_stages(p);
    p->uniforms = nullptr;
    p->attribs = nullptr;
    p->ublock = nullptr;
    p->log = nullptr;
    p->uniform_count = p->attrib_count = 0;
    p->attrib_mask = 0;
    p->cg_attribs = p->cg_slots = 0;
}

static void destroy_program(GlContext *ctx, GlProgram *p)
{
    gl_program_ready(p);
    for (uint32_t i = 0; i < p->shader_count; i++)
        release_shader(ctx, p->shaders[i]);
    if (p->id) {
        gl_device_lock();
        MimeGpuObject *o = gl_emit(MIME_GPU_PROGRAM_DESTROY, sizeof(MimeGpuObject));
        o->id = p->id;
        gl_device_unlock();
        gl_free_id(ID_PROGRAM, p->id);
    }
    gl_heap_free(&p->info, gl_batch_fence());
    free_reflection(p);
    gl_free(p->bindings);
    gl_free(p->frag_bindings);
    gl_program_set_stage_sources(p, nullptr, nullptr, nullptr);
    gl_program_set_feedback_names(p, 0, nullptr, 0);
    gl_free(p->main_source[0]);
    gl_free(p->main_source[1]);
    for (int k = 0; k < 5; k++)
        gl_subroutines_free(p->subroutines[k]);
    gl_name_set(&ctx->shared->programs, p->name, nullptr);
    gl_name_set(&ctx->shared->shaders, p->name, nullptr);
    gl_free(p);
}

/* frees an unnamed program such as the fixed function one: its host object and memory. */
void gl_program_free_internal(GlProgram *p)
{
    gl_program_ready(p);
    if (p->id) {
        gl_device_lock();
        MimeGpuObject *o = gl_emit(MIME_GPU_PROGRAM_DESTROY, sizeof(MimeGpuObject));
        o->id = p->id;
        gl_device_unlock();
        gl_free_id(ID_PROGRAM, p->id);
    }
    gl_heap_free(&p->info, gl_batch_fence());
    free_reflection(p);
    gl_free(p->bindings);
    gl_program_set_stage_sources(p, nullptr, nullptr, nullptr);
    gl_program_set_feedback_names(p, 0, nullptr, 0);
    gl_free(p);
}

GLAPI void APIENTRY glDeleteProgram(GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, name);
    if (!p)
        return;
    if (ctx->program == name) {
        p->deleted = TRUE;
        return;
    }
    destroy_program(ctx, p);
}

GLAPI GLboolean APIENTRY glIsProgram(GLuint name)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    return gl_program(ctx, name) ? GL_TRUE : GL_FALSE;
}

GLAPI void APIENTRY glAttachShader(GLuint program, GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, program);
    GlShader *s = shader(ctx, name);
    if (!p || !s || p->shader_count >= 8) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    for (uint32_t i = 0; i < p->shader_count; i++)
        if (p->shaders[i] == s)
            return;
    s->refs++;
    p->shaders[p->shader_count++] = s;
}

GLAPI void APIENTRY glDetachShader(GLuint program, GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, program);
    GlShader *s = shader(ctx, name);
    if (!p || !s)
        return;
    for (uint32_t i = 0; i < p->shader_count; i++) {
        if (p->shaders[i] == s) {
            p->shaders[i] = p->shaders[--p->shader_count];
            release_shader(ctx, s);
            return;
        }
    }
}

GLAPI void APIENTRY glGetAttachedShaders(GLuint program, GLsizei max, GLsizei *count, GLuint *shaders)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, program);
    GLsizei n = 0;
    for (uint32_t i = 0; p && i < p->shader_count && n < max; i++)
        shaders[n++] = p->shaders[i]->name;
    if (count)
        *count = n;
}

GLAPI void APIENTRY glBindAttribLocation(GLuint program, GLuint index, const GLchar *name)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, program);
    if (!p || !name || index >= MIME_GPU_MAX_ATTRIBS) {
        gl_error(ctx, p ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
        return;
    }
    char loc[12];
    int ln = _snprintf(loc, sizeof(loc), "%u", index);
    SIZE_T nn = strlen(name);
    char *b = gl_realloc(p->bindings, p->bindings_len + nn + 1 + (SIZE_T)ln + 1);
    if (!b)
        return;
    memcpy(b + p->bindings_len, name, nn + 1);
    memcpy(b + p->bindings_len + nn + 1, loc, (SIZE_T)ln + 1);
    p->bindings = b;
    p->bindings_len += (uint32_t)(nn + 1 + (SIZE_T)ln + 1);
}

GLAPI void APIENTRY glBindFragDataLocationIndexed(GLuint program, GLuint colorNumber, GLuint index, const GLchar *name)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, program);
    if (!p || !name) {
        gl_error(ctx, p ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
        return;
    }
    if (index > 1 || (index == 1 && colorNumber >= 1) || colorNumber >= MIME_GPU_MAX_COLORS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!strncmp(name, "gl_", 3)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    /* a name bound twice keeps the later place. */
    SIZE_T nn = strlen(name);
    for (uint32_t at = 0; at < p->frag_bindings_len;) {
        char *entry = p->frag_bindings + at;
        SIZE_T l0 = strlen(entry) + 1, l1 = strlen(entry + l0) + 1, l2 = strlen(entry + l0 + l1) + 1;
        if (l0 - 1 == nn && !memcmp(entry, name, nn)) {
            memmove(entry, entry + l0 + l1 + l2, p->frag_bindings_len - at - (uint32_t)(l0 + l1 + l2));
            p->frag_bindings_len -= (uint32_t)(l0 + l1 + l2);
            break;
        }
        at += (uint32_t)(l0 + l1 + l2);
    }
    char loc[12], idx[12];
    int ln = _snprintf(loc, sizeof(loc), "%u", colorNumber), in = _snprintf(idx, sizeof(idx), "%u", index);
    char *b = gl_realloc(p->frag_bindings, p->frag_bindings_len + nn + 1 + (SIZE_T)ln + 1 + (SIZE_T)in + 1);
    if (!b)
        return;
    char *at = b + p->frag_bindings_len;
    memcpy(at, name, nn + 1);
    memcpy(at + nn + 1, loc, (SIZE_T)ln + 1);
    memcpy(at + nn + 1 + ln + 1, idx, (SIZE_T)in + 1);
    p->frag_bindings = b;
    p->frag_bindings_len += (uint32_t)(nn + 1 + (SIZE_T)ln + 1 + (SIZE_T)in + 1);
}

GLAPI void APIENTRY glBindFragDataLocation(GLuint program, GLuint color, const GLchar *name)
{
    glBindFragDataLocationIndexed(program, color, 0, name);
}

/* the place an output was bound to: its location and index, or -1 when the application did not bind it. */
static BOOL frag_binding(const GlProgram *p, const GLchar *name, GLint *location, GLint *index)
{
    SIZE_T nn = strlen(name);
    for (uint32_t at = 0; at < p->frag_bindings_len;) {
        const char *entry = p->frag_bindings + at;
        SIZE_T l0 = strlen(entry) + 1, l1 = strlen(entry + l0) + 1, l2 = strlen(entry + l0 + l1) + 1;
        if (l0 - 1 == nn && !memcmp(entry, name, nn)) {
            *location = 0;
            for (const char *d = entry + l0; *d; d++)
                *location = *location * 10 + (*d - '0');
            *index = entry[l0 + l1] == '1';
            return TRUE;
        }
        at += (uint32_t)(l0 + l1 + l2);
    }
    return FALSE;
}

GLAPI GLint APIENTRY glGetFragDataLocation(GLuint program, const GLchar *name)
{
    GL_CONTEXT_OR_RETURN(-1);
    GlProgram *p = gl_program(ctx, program);
    if (!p || !name || !gl_program_ready(p)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return -1;
    }
    GLint location, index;
    return frag_binding(p, name, &location, &index) ? location : -1;
}

GLAPI GLint APIENTRY glGetFragDataIndex(GLuint program, const GLchar *name)
{
    GL_CONTEXT_OR_RETURN(-1);
    GlProgram *p = gl_program(ctx, program);
    if (!p || !name || !gl_program_ready(p)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return -1;
    }
    GLint location, index;
    return frag_binding(p, name, &location, &index) ? index : -1;
}

void gl_program_free_stages_only(GlProgram *p)
{
    free_reflection(p);
}

/* copies the values of the uniforms src holds into those of dst that have the same name. */
void gl_program_copy_uniforms(GlProgram *dst, const GlProgram *src)
{
    if (!dst->ublock || !src->ublock)
        return;
    for (uint32_t i = 0; i < src->uniform_count; i++) {
        const GlUniform *a = &src->uniforms[i];
        for (uint32_t k = 0; k < dst->uniform_count; k++) {
            GlUniform *b = &dst->uniforms[k];
            if (strcmp(a->name, b->name) || a->type != b->type || a->size != b->size)
                continue;
            if (a->units && b->units) {
                memcpy(b->units, a->units, (SIZE_T)a->size * sizeof(GLint));
            } else if (!a->units && !b->units) {
                int cols, rows, class;
                shape_of(a->type, &cols, &rows, &class);
                uint32_t size = class == 2 ? 8 : 4;
                uint32_t mstride = a->matrix_stride ? a->matrix_stride : (uint32_t)rows * size;
                uint32_t extent = cols > 1 ? (uint32_t)(cols - 1) * mstride + (uint32_t)rows * size : (uint32_t)rows * size;
                uint32_t astride = a->array_stride ? a->array_stride : (cols > 1 ? mstride * (uint32_t)cols : (uint32_t)rows * size);
                uint32_t bstride = b->array_stride ? b->array_stride : astride;
                for (GLint e = 0; e < a->size; e++) {
                    uint32_t from = a->offset + (uint32_t)e * astride, to = b->offset + (uint32_t)e * bstride;
                    if (from + extent <= src->ublock_size + 16 && to + extent <= dst->ublock_size + 16)
                        memcpy(dst->ublock + to, src->ublock + from, extent);
                }
            }
            break;
        }
    }
    dst->ublock_dirty = TRUE;
}

/* sends both stages to the host compiler; the program is usable once gl_program_ready says so. */
static BOOL link_program(GlProgram *p, const char *vs, const char *fs, BOOL compute)
{
    static uint32_t link_serial;
    gl_program_ready(p);
    free_reflection(p);
    p->linked = p->link_status = FALSE;
    gl_device_lock();
    p->serial = ++link_serial;
    gl_device_unlock();
    p->compute = compute;
    if (compute ? !vs : (!vs || !fs)) {
        p->log = gl_strdup(compute ? "no compute shader\n" : vs ? "no fragment shader\n" : "no vertex shader\n");
        p->linked = TRUE;
        return FALSE;
    }
    if (!p->info.ptr && !gl_heap_alloc(INFO_CAPACITY, &p->info)) {
        p->log = gl_strdup("out of memory\n");
        p->linked = TRUE;
        return FALSE;
    }
    if (!p->id)
        p->id = gl_new_id(ID_PROGRAM);
    ((MimeGpuProgramInfo *)p->info.ptr)->size = 0;
    uint32_t vn = (uint32_t)strlen(vs), fn = compute ? 0 : (uint32_t)strlen(fs);
    gl_device_lock();
    if (!compute)
        gl_program_send_stages(p);
    MimeGpuProgramCreate *c = gl_emit(compute ? MIME_GPU_COMPUTE_PROGRAM_CREATE : MIME_GPU_PROGRAM_CREATE,
                                      (uint32_t)sizeof(MimeGpuProgramCreate) + vn + fn + p->bindings_len);
    c->id = p->id;
    c->vertex_bytes = vn;
    c->fragment_bytes = fn;
    c->bindings_bytes = p->bindings_len;
    c->result = gl_mem(&p->info);
    c->result_capacity = INFO_CAPACITY;
    char *body = (char *)(c + 1);
    memcpy(body, vs, vn);
    memcpy(body + vn, fs, fn);
    if (p->bindings_len)
        memcpy(body + vn + fn, p->bindings, p->bindings_len);
    gl_publish();
    gl_device_unlock();
    p->linking = TRUE;
    return TRUE;
}

/* links the sources of the stages, which have had their subroutines taken out. */
static void link_stages(GlProgram *p, const char *vs, const char *fs, const char *control, const char *evaluation,
                        const char *geometry)
{
    gl_program_set_stage_sources(p, control, evaluation, geometry);
    gl_free(p->main_source[0]);
    gl_free(p->main_source[1]);
    p->main_source[0] = vs ? gl_strdup(vs) : nullptr;
    p->main_source[1] = fs ? gl_strdup(fs) : nullptr;
    p->stage_mask = (vs ? 1u : 0) | (control ? 2u : 0) | (evaluation ? 4u : 0) | (geometry ? 8u : 0) | (fs ? 16u : 0);
    if (p->separable && (!control != !evaluation)) {
        /* a lone tessellation stage is linked with its partner in a pipeline, there is nothing for the host to build. */
        static uint32_t lone_serial = 0x80000000u;
        gl_program_ready(p);
        gl_program_free_stages_only(p);
        p->linked = p->link_status = TRUE;
        p->serial = ++lone_serial;
        return;
    }
    if (!gl_program_stages_ok(p, control, evaluation, geometry))
        return;
    /* a separable program of one stage is built with stand-ins for the others. */
    char *standin = nullptr;
    if (p->separable) {
        if (!vs)
            vs = fs ? (standin = gl_standin_vertex(fs)) : "#version 330 core\nvoid main() { gl_Position = vec4(0.0); }\n";
        if (!fs)
            fs = "#version 330 core\nvoid main() { }\n";
    }
    link_program(p, vs, fs, FALSE);
    gl_free(standin);
}

BOOL gl_program_link_sources(GlProgram *p, const char *vs, const char *fs)
{
    return link_program(p, vs, fs, FALSE);
}

GLAPI void APIENTRY glLinkProgram(GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, name);
    if (!p) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    /* the stages in the order of GlProgram::subroutines. */
    const char *source[5] = {};
    for (uint32_t i = 0; i < p->shader_count; i++) {
        GlShader *s = p->shaders[i];
        if (s->type == GL_COMPUTE_SHADER)
            continue;
        int k = s->type == GL_VERTEX_SHADER ? 0 : s->type == GL_TESS_CONTROL_SHADER ? 1
                : s->type == GL_TESS_EVALUATION_SHADER ? 2 : s->type == GL_GEOMETRY_SHADER ? 3 : 4;
        if (!source[k])
            source[k] = s->source;
    }
    if (ctx->profile == WGL_CONTEXT_CORE_PROFILE_BIT_ARB) {
        for (int k = 0; k < 5; k++) {
            if (!source[k] || !gl_cg_core_violation(source[k]))
                continue;
            gl_program_ready(p);
            free_reflection(p);
            p->linked = TRUE;
            p->link_status = FALSE;
            p->log = gl_strdup("a built-in variable of the compatibility profile is used in a core profile context\n");
            return;
        }
    }
    char *rewritten[5] = {};
    for (int k = 0; k < 5; k++) {
        GlSubroutines *info = nullptr;
        if (source[k]) {
            rewritten[k] = gl_subroutine_rewrite(source[k], k, &info);
            if (rewritten[k])
                source[k] = rewritten[k];
        }
        gl_subroutines_free(p->subroutines[k]);
        p->subroutines[k] = info;
    }
    const char *cs = nullptr;
    for (uint32_t i = 0; i < p->shader_count; i++)
        if (p->shaders[i]->type == GL_COMPUTE_SHADER && !cs)
            cs = p->shaders[i]->source;
    if (cs && (source[0] || source[4] || source[1] || source[2] || source[3])) {
        gl_program_ready(p);
        free_reflection(p);
        p->linked = TRUE;
        p->link_status = FALSE;
        p->log = gl_strdup("a compute shader cannot be linked with other stages\n");
    } else if (cs) {
        link_program(p, cs, nullptr, TRUE);
    } else {
        link_stages(p, source[0], source[4], source[1], source[2], source[3]);
    }
    for (int k = 0; k < 5; k++)
        gl_free(rewritten[k]);
}

void gl_program_set_int(GlContext *ctx, GlProgram *p, GLint location, GLint value)
{
    put_uniform(ctx, p, location, 1, 1, &value, VALUES_INT);
}

void gl_program_get_int(GlProgram *p, GLint location, GLint *value)
{
    *value = 0;
    uint32_t i = (uint32_t)location / 4096, element = (uint32_t)location % 4096;
    if (i >= p->uniform_count || element >= (uint32_t)p->uniforms[i].size)
        return;
    const GlUniform *u = &p->uniforms[i];
    uint32_t stride = u->array_stride ? u->array_stride : 4;
    if (u->offset + (element + 1) * stride <= p->ublock_size + 16)
        memcpy(value, p->ublock + u->offset + element * stride, 4);
}

/* reads the host's reflection once the link finished. */
static void take_reflection(GlProgram *p)
{
    const uint8_t *base = p->info.ptr;
    const MimeGpuProgramInfo *info = (const MimeGpuProgramInfo *)base;
    p->link_status = info->status == 0;
    if (info->log_bytes)
        p->log = gl_strdup((const char *)base + info->log_offset);
    if (!p->link_status) {
        gl_log("program %u failed to link:\n%s", p->name, p->log ? p->log : "");
        return;
    }
    const MimeGpuAttribInfo *a = (const MimeGpuAttribInfo *)(base + info->attrib_offset);
    p->attribs = gl_alloc((info->attrib_count + 1) * sizeof(GlAttrib));
    for (uint32_t i = 0; i < info->attrib_count; i++) {
        /* the inputs the host made for the built-in variables of GLSL are not the program's own. */
        const char *attribute_name = (const char *)base + a[i].name_offset;
        if (!strncmp(attribute_name, MIME_GLC_PREFIX, sizeof(MIME_GLC_PREFIX) - 1)) {
            if (a[i].location < MIME_GPU_MAX_ATTRIBS) {
                p->cg_attribs |= 1u << a[i].location;
                p->attrib_mask |= 1u << a[i].location;
            }
            continue;
        }
        GlAttrib *x = &p->attribs[p->attrib_count++];
        x->name = gl_strdup(attribute_name);
        x->type = a[i].gl_type;
        x->size = (GLint)a[i].array_size;
        x->location = (GLint)a[i].location;
        BOOL mat = (x->type >= GL_FLOAT_MAT2 && x->type <= GL_FLOAT_MAT4) ||
                   (x->type >= GL_FLOAT_MAT2x3 && x->type <= GL_FLOAT_MAT4x3);
        x->slots = (mat ? (x->type == GL_FLOAT_MAT2 || x->type == GL_FLOAT_MAT2x3 || x->type == GL_FLOAT_MAT2x4 ? 2
                           : x->type == GL_FLOAT_MAT3 || x->type == GL_FLOAT_MAT3x2 || x->type == GL_FLOAT_MAT3x4 ? 3
                                                                                                                  : 4)
                        : 1) *
                   (uint32_t)x->size;
        for (uint32_t s = 0; s < x->slots && x->location + s < MIME_GPU_MAX_ATTRIBS; s++)
            p->attrib_mask |= 1u << (x->location + s);
    }
    const MimeGpuUniformInfo *u = (const MimeGpuUniformInfo *)(base + info->uniform_offset);
    p->uniforms = gl_alloc((info->uniform_count + 1) * sizeof(GlUniform));
    for (uint32_t i = 0; i < info->uniform_count; i++) {
        const char *uniform_name = (const char *)base + u[i].name_offset;
        if (!strncmp(uniform_name, MIME_GLC_PREFIX, sizeof(MIME_GLC_PREFIX) - 1)) {
            if (!strncmp(uniform_name, MIME_GLC_ARRAY, sizeof(MIME_GLC_ARRAY) - 1)) {
                p->cg_offset = u[i].offset;
                p->cg_stride = u[i].array_stride ? u[i].array_stride : 16;
                p->cg_slots = u[i].array_size;
            }
            continue;
        }
        GlUniform *x = &p->uniforms[p->uniform_count++];
        x->name = gl_strdup((const char *)base + u[i].name_offset);
        SIZE_T n = strlen(x->name);
        x->base_len = n > 3 && !strcmp(x->name + n - 3, "[0]") ? n - 3 : n;
        x->type = u[i].gl_type;
        x->size = (GLint)u[i].array_size;
        x->offset = u[i].offset;
        x->array_stride = u[i].array_stride;
        x->matrix_stride = u[i].matrix_stride;
        x->texture[0] = u[i].texture[0];
        x->texture[1] = u[i].texture[1];
        x->binding = u[i].binding;
        x->block = u[i].block;
        if (x->texture[0] >= 0 || x->texture[1] >= 0) {
            x->units = gl_alloc((SIZE_T)x->size * sizeof(GLint));
            /* layout(binding = n) is the first value of the uniform. */
            for (GLint e = 0; x->units && x->binding >= 0 && e < x->size; e++)
                x->units[e] = x->binding + e;
        }
    }
    p->local_size[0] = info->local_size[0];
    p->local_size[1] = info->local_size[1];
    p->local_size[2] = info->local_size[2];
    const MimeGpuStorageBlockInfo *sb = (const MimeGpuStorageBlockInfo *)(base + info->storage_offset);
    p->storage = gl_alloc((info->storage_count + 1) * sizeof(GlStorageBlock));
    p->counter_buffers = gl_alloc((info->storage_count + 1) * sizeof(GlCounterBuffer));
    for (uint32_t i = 0; i < info->storage_count; i++) {
        if (sb[i].atomic) {
            GlCounterBuffer *buffer = &p->counter_buffers[p->counter_buffer_count++];
            buffer->slot = i;
            buffer->binding = sb[i].binding >= 0 ? (GLuint)sb[i].binding : 0;
            buffer->stages = sb[i].stages;
            continue;
        }
        GlStorageBlock *block = &p->storage[p->storage_count++];
        block->name = gl_strdup((const char *)base + sb[i].name_offset);
        block->bytes = sb[i].bytes;
        block->stages = sb[i].stages;
        block->variables = sb[i].variables;
        block->binding = sb[i].binding >= 0 ? (GLuint)sb[i].binding : 0;
        block->slot = i;
    }
    const MimeGpuCounterInfo *ci = (const MimeGpuCounterInfo *)(base + info->counter_offset);
    p->counters = gl_alloc((info->counter_count + 1) * sizeof(GlCounter));
    for (uint32_t i = 0; i < info->counter_count; i++) {
        GlCounter *counter = &p->counters[p->counter_count++];
        counter->name = gl_strdup((const char *)base + ci[i].name_offset);
        counter->binding = ci[i].binding;
        counter->offset = ci[i].offset;
        counter->size = ci[i].array_size ? ci[i].array_size : 1;
        counter->stages = ci[i].stages;
        for (uint32_t b = 0; b < p->counter_buffer_count; b++) {
            GlCounterBuffer *buffer = &p->counter_buffers[b];
            if (buffer->binding != counter->binding)
                continue;
            buffer->counters++;
            if (counter->offset + 4 * counter->size > buffer->bytes)
                buffer->bytes = counter->offset + 4 * counter->size;
        }
    }
    p->ublock_size = info->uniform_bytes;
    const MimeGpuUniformBlockInfo *blocks = (const MimeGpuUniformBlockInfo *)(base + info->block_offset);
    p->blocks = gl_alloc((info->block_count + 1) * sizeof(GlUniformBlock));
    for (uint32_t i = 0; i < info->block_count; i++) {
        GlUniformBlock *block = &p->blocks[p->block_count++];
        block->name = gl_strdup((const char *)base + blocks[i].name_offset);
        block->bytes = blocks[i].bytes;
        block->stages = blocks[i].stages;
    }
    p->ublock = gl_alloc(p->ublock_size + 16);
    p->ublock_dirty = TRUE;
    gl_program_take_stages(p, base, info);
}

/* waits for an asynchronous link; TRUE when the program is usable. */
BOOL gl_program_ready(GlProgram *p)
{
    if (p->linking) {
        volatile MimeGpuProgramInfo *info = (volatile MimeGpuProgramInfo *)p->info.ptr;
        for (int spin = 0; !__atomic_load_n(&info->size, __ATOMIC_ACQUIRE); spin++) {
            if (GlDev.hdr->lost)
                break;
            if (spin < 1000) {
                NtYieldExecution();
            } else {
                LARGE_INTEGER t = {.QuadPart = -10000};
                NtDelayExecution(FALSE, &t);
            }
        }
        p->linking = FALSE;
        p->linked = TRUE;
        if (info->size)
            take_reflection(p);
    }
    return p->linked && p->link_status;
}

GLAPI void APIENTRY glUseProgram(GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = name ? gl_program(ctx, name) : nullptr;
    if (name && !p) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    GlProgram *old = ctx->current_program;
    if (old && old->deleted && old != p) {
        ctx->current_program = nullptr;
        destroy_program(ctx, old);
    }
    ctx->program = name;
    ctx->current_program = p;
    ctx->dirty |= DIRTY_PIPELINE | DIRTY_UNIFORMS | DIRTY_TEXTURES;
}

GLAPI void APIENTRY glValidateProgram(GLuint name)
{
}

static SIZE_T binary_size(const GlProgram *p);

GLAPI void APIENTRY glGetProgramiv(GLuint name, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, name);
    if (!p) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    gl_program_ready(p);
    switch (pname) {
    case GL_LINK_STATUS:
        *params = p->link_status;
        break;
    case GL_VALIDATE_STATUS:
        *params = p->link_status;
        break;
    case GL_DELETE_STATUS:
        *params = p->deleted;
        break;
    case GL_INFO_LOG_LENGTH:
        *params = p->log ? (GLint)strlen(p->log) + 1 : 0;
        break;
    case GL_ATTACHED_SHADERS:
        *params = (GLint)p->shader_count;
        break;
    case GL_ACTIVE_ATTRIBUTES:
        *params = (GLint)p->attrib_count;
        break;
    case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH: {
        GLint m = 0;
        for (uint32_t i = 0; i < p->attrib_count; i++)
            if ((GLint)strlen(p->attribs[i].name) + 1 > m)
                m = (GLint)strlen(p->attribs[i].name) + 1;
        *params = m;
        break;
    }
    case GL_ACTIVE_UNIFORMS:
        *params = (GLint)p->uniform_count;
        break;
    case GL_ACTIVE_UNIFORM_MAX_LENGTH: {
        GLint m = 0;
        for (uint32_t i = 0; i < p->uniform_count; i++)
            if ((GLint)strlen(p->uniforms[i].name) + 1 > m)
                m = (GLint)strlen(p->uniforms[i].name) + 1;
        *params = m;
        break;
    }
    case GL_ACTIVE_UNIFORM_BLOCKS:
        *params = (GLint)p->block_count;
        break;
    case GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH: {
        GLint longest = 0;
        for (uint32_t i = 0; i < p->block_count; i++) {
            GLint length = (GLint)strlen(p->blocks[i].name) + 1;
            if (length > longest) longest = length;
        }
        *params = longest;
        break;
    }
    case GL_PROGRAM_BINARY_LENGTH:
        *params = p->link_status ? (GLint)binary_size(p) : 0;
        break;
    case GL_COMPUTE_WORK_GROUP_SIZE:
        if (!p->compute || !p->link_status) {
            gl_error(ctx, GL_INVALID_OPERATION);
            break;
        }
        for (int i = 0; i < 3; i++)
            params[i] = (GLint)p->local_size[i];
        break;
    case GL_ACTIVE_ATOMIC_COUNTER_BUFFERS:
        *params = (GLint)p->counter_buffer_count;
        break;
    default:
        if (!gl_program_query_stage(p, pname, params))
            gl_error(ctx, GL_INVALID_ENUM);
        break;
    }
}

GLAPI void APIENTRY glGetProgramInfoLog(GLuint name, GLsizei size, GLsizei *length, GLchar *log)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, name);
    if (p)
        gl_program_ready(p);
    SIZE_T n = p && p->log ? strlen(p->log) : 0;
    if (size > 0) {
        if (n >= (SIZE_T)size)
            n = (SIZE_T)size - 1;
        if (n)
            memcpy(log, p->log, n);
        log[n] = 0;
    }
    if (length)
        *length = (GLsizei)n;
}

GLAPI GLint APIENTRY glGetAttribLocation(GLuint name, const GLchar *attrib)
{
    GL_CONTEXT_OR_RETURN(-1);
    GlProgram *p = gl_program(ctx, name);
    if (!p || !gl_program_ready(p))
        return -1;
    for (uint32_t i = 0; i < p->attrib_count; i++)
        if (!strcmp(p->attribs[i].name, attrib))
            return p->attribs[i].location;
    return -1;
}

static void copy_name(const char *src, GLsizei size, GLsizei *length, GLchar *dst)
{
    SIZE_T n = strlen(src);
    if (size > 0) {
        if (n >= (SIZE_T)size)
            n = (SIZE_T)size - 1;
        memcpy(dst, src, n);
        dst[n] = 0;
    } else {
        n = 0;
    }
    if (length)
        *length = (GLsizei)n;
}

GLAPI void APIENTRY glGetActiveAttrib(GLuint name, GLuint index, GLsizei size, GLsizei *length, GLint *count,
                                      GLenum *type, GLchar *out)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, name);
    if (!p || !gl_program_ready(p) || index >= p->attrib_count) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    copy_name(p->attribs[index].name, size, length, out);
    *count = p->attribs[index].size;
    *type = p->attribs[index].type;
}

GLAPI void APIENTRY glGetActiveUniform(GLuint name, GLuint index, GLsizei size, GLsizei *length, GLint *count,
                                       GLenum *type, GLchar *out)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, name);
    if (!p || !gl_program_ready(p) || index >= p->uniform_count) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    copy_name(p->uniforms[index].name, size, length, out);
    *count = p->uniforms[index].size;
    *type = p->uniforms[index].type;
}

GLAPI void APIENTRY glGetActiveUniformsiv(GLuint name, GLsizei count, const GLuint *indices, GLenum pname,
                                          GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, name);
    if (!p || !gl_program_ready(p))
        return;
    for (GLsizei i = 0; i < count; i++) {
        if (indices[i] >= p->uniform_count) {
            params[i] = 0;
            continue;
        }
        GlUniform *u = &p->uniforms[indices[i]];
        switch (pname) {
        case GL_UNIFORM_TYPE:
            params[i] = (GLint)u->type;
            break;
        case GL_UNIFORM_SIZE:
            params[i] = u->size;
            break;
        case GL_UNIFORM_NAME_LENGTH:
            params[i] = (GLint)strlen(u->name) + 1;
            break;
        case GL_UNIFORM_BLOCK_INDEX:
            params[i] = (GLint)u->block - 1;
            break;
        case GL_UNIFORM_IS_ROW_MAJOR:
            params[i] = 0;
            break;
        case GL_UNIFORM_ATOMIC_COUNTER_BUFFER_INDEX:
            params[i] = -1;
            break;
        case GL_UNIFORM_OFFSET:
            params[i] = (GLint)u->offset;
            break;
        case GL_UNIFORM_ARRAY_STRIDE:
            params[i] = (GLint)u->array_stride;
            break;
        case GL_UNIFORM_MATRIX_STRIDE:
            params[i] = (GLint)u->matrix_stride;
            break;
        default:
            params[i] = 0;
            break;
        }
    }
}

GLAPI GLint APIENTRY glGetUniformLocation(GLuint name, const GLchar *uniform)
{
    GL_CONTEXT_OR_RETURN(-1);
    GlProgram *p = gl_program(ctx, name);
    if (!p || !gl_program_ready(p) || !uniform)
        return -1;
    /* "name", "name[0]" and "name[i]" all reach an array. */
    SIZE_T n = strlen(uniform);
    GLint element = 0;
    if (n && uniform[n - 1] == ']') {
        const char *br = strrchr(uniform, '[');
        if (br) {
            element = 0;
            for (const char *d = br + 1; *d >= '0' && *d <= '9'; d++)
                element = element * 10 + (*d - '0');
            n = (SIZE_T)(br - uniform);
        }
    }
    for (uint32_t i = 0; i < p->uniform_count; i++) {
        GlUniform *u = &p->uniforms[i];
        if (u->block)
            continue;
        if (u->base_len == n && !strncmp(u->name, uniform, n) && element < u->size)
            return (GLint)(i * 4096 + (uint32_t)element);
        /* a struct member path written in full. */
        if (!strcmp(u->name, uniform))
            return (GLint)(i * 4096);
    }
    return -1;
}

GLAPI GLuint APIENTRY glGetUniformBlockIndex(GLuint program, const GLchar *name)
{
    GL_CONTEXT_OR_RETURN(GL_INVALID_INDEX);
    GlProgram *p = gl_program(ctx, program);
    if (!p || !gl_program_ready(p)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return GL_INVALID_INDEX;
    }
    for (uint32_t i = 0; name && i < p->block_count; i++)
        if (!strcmp(name, p->blocks[i].name))
            return i;
    return GL_INVALID_INDEX;
}

GLAPI void APIENTRY glUniformBlockBinding(GLuint program, GLuint index, GLuint binding)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, program);
    if (!p || !gl_program_ready(p)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (index >= p->block_count || binding >= MIME_GPU_MAX_UNIFORM_BLOCKS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    p->blocks[index].binding = binding;
}

GLAPI void APIENTRY glGetActiveUniformBlockiv(GLuint program, GLuint index, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, program);
    if (!p || !gl_program_ready(p) || index >= p->block_count) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!params) return;
    GlUniformBlock *block = &p->blocks[index];
    switch (pname) {
    case GL_UNIFORM_BLOCK_BINDING: *params = (GLint)block->binding; break;
    case GL_UNIFORM_BLOCK_DATA_SIZE: *params = (GLint)block->bytes; break;
    case GL_UNIFORM_BLOCK_NAME_LENGTH: *params = (GLint)strlen(block->name) + 1; break;
    case GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS:
        *params = 0;
        for (uint32_t i = 0; i < p->uniform_count; i++)
            *params += p->uniforms[i].block == index + 1;
        break;
    case GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES:
        for (uint32_t i = 0; i < p->uniform_count; i++)
            if (p->uniforms[i].block == index + 1)
                *params++ = (GLint)i;
        break;
    case GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER: *params = (block->stages & 1) != 0; break;
    case GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER: *params = (block->stages & 2) != 0; break;
    case GL_UNIFORM_BLOCK_REFERENCED_BY_TESS_CONTROL_SHADER: *params = (block->stages & 4) != 0; break;
    case GL_UNIFORM_BLOCK_REFERENCED_BY_TESS_EVALUATION_SHADER: *params = (block->stages & 8) != 0; break;
    case GL_UNIFORM_BLOCK_REFERENCED_BY_GEOMETRY_SHADER: *params = (block->stages & 16) != 0; break;
    default: gl_error(ctx, GL_INVALID_ENUM); break;
    }
}

GLAPI void APIENTRY glGetActiveUniformBlockName(GLuint program, GLuint index, GLsizei size, GLsizei *length, GLchar *name)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, program);
    if (size < 0 || !p || !gl_program_ready(p) || index >= p->block_count) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    SIZE_T count = size > 0 ? strlen(p->blocks[index].name) : 0;
    if (size > 0 && count >= (SIZE_T)size) count = (SIZE_T)size - 1;
    if (size > 0 && name) {
        memcpy(name, p->blocks[index].name, count);
        name[count] = 0;
    }
    if (length) *length = (GLsizei)count;
}

/* ---------------------------------------------------------------- uniforms */

static GlUniform *lookup(GlContext *ctx, GlProgram *p, GLint location, GLint *element)
{
    if (!p || location < 0 || !gl_program_ready(p))
        return nullptr;
    uint32_t i = (uint32_t)location / 4096;
    *element = location % 4096;
    if (i >= p->uniform_count || *element >= p->uniforms[i].size) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return nullptr;
    }
    return &p->uniforms[i];
}

/* columns and rows of a uniform type and the class of its components: 0 integer, 1 float, 2 double. */
void shape_of(GLenum type, int *cols, int *rows, int *class);
static void shape(GLenum type, int *cols, int *rows, int *class)
{
    *cols = 1;
    *class = 1;
    switch (type) {
    case GL_FLOAT:
        *rows = 1;
        break;
    case GL_FLOAT_VEC2:
        *rows = 2;
        break;
    case GL_FLOAT_VEC3:
        *rows = 3;
        break;
    case GL_FLOAT_VEC4:
        *rows = 4;
        break;
    case GL_DOUBLE:
        *rows = 1;
        *class = 2;
        break;
    case GL_DOUBLE_VEC2:
        *rows = 2;
        *class = 2;
        break;
    case GL_DOUBLE_VEC3:
        *rows = 3;
        *class = 2;
        break;
    case GL_DOUBLE_VEC4:
        *rows = 4;
        *class = 2;
        break;
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_BOOL:
        *rows = 1;
        *class = 0;
        break;
    case GL_INT_VEC2:
    case GL_UNSIGNED_INT_VEC2:
    case GL_BOOL_VEC2:
        *rows = 2;
        *class = 0;
        break;
    case GL_INT_VEC3:
    case GL_UNSIGNED_INT_VEC3:
    case GL_BOOL_VEC3:
        *rows = 3;
        *class = 0;
        break;
    case GL_INT_VEC4:
    case GL_UNSIGNED_INT_VEC4:
    case GL_BOOL_VEC4:
        *rows = 4;
        *class = 0;
        break;
    case GL_FLOAT_MAT2:
    case GL_DOUBLE_MAT2:
        *cols = *rows = 2;
        break;
    case GL_FLOAT_MAT3:
    case GL_DOUBLE_MAT3:
        *cols = *rows = 3;
        break;
    case GL_FLOAT_MAT4:
    case GL_DOUBLE_MAT4:
        *cols = *rows = 4;
        break;
    case GL_FLOAT_MAT2x3:
    case GL_DOUBLE_MAT2x3:
        *cols = 2;
        *rows = 3;
        break;
    case GL_FLOAT_MAT2x4:
    case GL_DOUBLE_MAT2x4:
        *cols = 2;
        *rows = 4;
        break;
    case GL_FLOAT_MAT3x2:
    case GL_DOUBLE_MAT3x2:
        *cols = 3;
        *rows = 2;
        break;
    case GL_FLOAT_MAT3x4:
    case GL_DOUBLE_MAT3x4:
        *cols = 3;
        *rows = 4;
        break;
    case GL_FLOAT_MAT4x2:
    case GL_DOUBLE_MAT4x2:
        *cols = 4;
        *rows = 2;
        break;
    case GL_FLOAT_MAT4x3:
    case GL_DOUBLE_MAT4x3:
        *cols = 4;
        *rows = 3;
        break;
    default:
        *rows = 1;
        *class = 0;
        break;
    }
    if (type >= GL_DOUBLE_MAT2 && type <= GL_DOUBLE_MAT4x3)
        *class = 2;
}

void shape_of(GLenum type, int *cols, int *rows, int *class)
{
    shape(type, cols, rows, class);
}

/* the program a glUniform call without a program name writes: the one in use, else the active one of the pipeline. */
static GlProgram *uniform_program(GlContext *ctx)
{
    if (ctx->current_program)
        return ctx->current_program;
    return gl_pipeline_active_program(ctx);
}

static double value_at(const void *values, int kind, SIZE_T i)
{
    return kind == VALUES_DOUBLE ? ((const double *)values)[i]
           : kind == VALUES_FLOAT ? (double)((const float *)values)[i]
                                  : (double)((const int *)values)[i];
}

/* writes count elements of n components each; samplers take units. */
static void put_uniform(GlContext *ctx, GlProgram *p, GLint location, GLsizei count, int n, const void *values, int kind)
{
    GLint element;
    GlUniform *u = lookup(ctx, p, location, &element);
    if (!u)
        return;
    if (count > u->size - element)
        count = u->size - element;
    if (u->units) {
        for (GLsizei i = 0; i < count; i++)
            u->units[element + i] = (GLint)value_at(values, kind, (SIZE_T)i);
        p->uniform_version++;
        ctx->dirty |= DIRTY_TEXTURES;
        return;
    }
    int cols, rows, class;
    shape(u->type, &cols, &rows, &class);
    uint32_t size = class == 2 ? 8 : 4;
    uint32_t stride = u->array_stride ? u->array_stride : (uint32_t)rows * size;
    for (GLsizei i = 0; i < count; i++) {
        uint8_t *dst = p->ublock + u->offset + (uint32_t)(element + i) * stride;
        if (dst + (uint32_t)n * size > p->ublock + p->ublock_size)
            break;
        for (int c = 0; c < n; c++) {
            SIZE_T at = (SIZE_T)i * (SIZE_T)n + (SIZE_T)c;
            if (class == kind) {
                memcpy(dst + c * size, (const uint8_t *)values + at * size, size);
            } else if (class == 2) {
                double v = value_at(values, kind, at);
                memcpy(dst + c * 8, &v, 8);
            } else if (class == 1) {
                float v = (float)value_at(values, kind, at);
                memcpy(dst + c * 4, &v, 4);
            } else {
                GLint v = (GLint)value_at(values, kind, at);
                memcpy(dst + c * 4, &v, 4);
            }
        }
    }
    p->ublock_dirty = TRUE;
    p->uniform_version++;
    ctx->dirty |= DIRTY_UNIFORMS;
}

static void put_matrix(GlContext *ctx, GlProgram *p, GLint location, GLsizei count, int cols, int rows,
                       GLboolean transpose, const void *v, BOOL dbl)
{
    GLint element;
    GlUniform *u = lookup(ctx, p, location, &element);
    if (!u)
        return;
    if (count > u->size - element)
        count = u->size - element;
    uint32_t size = dbl ? 8 : 4;
    uint32_t mstride = u->matrix_stride ? u->matrix_stride : (uint32_t)rows * size;
    uint32_t astride = u->array_stride ? u->array_stride : mstride * (uint32_t)cols;
    for (GLsizei i = 0; i < count; i++) {
        const uint8_t *m = (const uint8_t *)v + (SIZE_T)i * (SIZE_T)(cols * rows) * size;
        uint8_t *dst = p->ublock + u->offset + (uint32_t)(element + i) * astride;
        if (dst + mstride * (uint32_t)cols > p->ublock + p->ublock_size + 16)
            break;
        for (int c = 0; c < cols; c++)
            for (int r = 0; r < rows; r++) {
                const uint8_t *x = m + (transpose ? (SIZE_T)(r * cols + c) : (SIZE_T)(c * rows + r)) * size;
                memcpy(dst + (uint32_t)c * mstride + (uint32_t)r * size, x, size);
            }
    }
    p->ublock_dirty = TRUE;
    p->uniform_version++;
    ctx->dirty |= DIRTY_UNIFORMS;
}

static void set_uniform(GLint location, GLsizei count, int n, const void *values, int kind)
{
    GL_CONTEXT_OR_RETURN();
    put_uniform(ctx, uniform_program(ctx), location, count, n, values, kind);
}

static void set_matrix(GLint location, GLsizei count, int cols, int rows, GLboolean transpose, const GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
    put_matrix(ctx, uniform_program(ctx), location, count, cols, rows, transpose, v, FALSE);
}

static void set_matrix_d(GLint location, GLsizei count, int cols, int rows, GLboolean transpose, const GLdouble *v)
{
    GL_CONTEXT_OR_RETURN();
    put_matrix(ctx, uniform_program(ctx), location, count, cols, rows, transpose, v, TRUE);
}

GLAPI void APIENTRY glUniform1f(GLint l, GLfloat a)
{
    set_uniform(l, 1, 1, &a, VALUES_FLOAT);
}

GLAPI void APIENTRY glUniform2f(GLint l, GLfloat a, GLfloat b)
{
    GLfloat v[2] = {a, b};
    set_uniform(l, 1, 2, v, VALUES_FLOAT);
}

GLAPI void APIENTRY glUniform3f(GLint l, GLfloat a, GLfloat b, GLfloat c)
{
    GLfloat v[3] = {a, b, c};
    set_uniform(l, 1, 3, v, VALUES_FLOAT);
}

GLAPI void APIENTRY glUniform4f(GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d)
{
    GLfloat v[4] = {a, b, c, d};
    set_uniform(l, 1, 4, v, VALUES_FLOAT);
}

GLAPI void APIENTRY glUniform1i(GLint l, GLint a)
{
    set_uniform(l, 1, 1, &a, VALUES_INT);
}

GLAPI void APIENTRY glUniform2i(GLint l, GLint a, GLint b)
{
    GLint v[2] = {a, b};
    set_uniform(l, 1, 2, v, VALUES_INT);
}

GLAPI void APIENTRY glUniform3i(GLint l, GLint a, GLint b, GLint c)
{
    GLint v[3] = {a, b, c};
    set_uniform(l, 1, 3, v, VALUES_INT);
}

GLAPI void APIENTRY glUniform4i(GLint l, GLint a, GLint b, GLint c, GLint d)
{
    GLint v[4] = {a, b, c, d};
    set_uniform(l, 1, 4, v, VALUES_INT);
}

GLAPI void APIENTRY glUniform1ui(GLint l, GLuint a)
{
    set_uniform(l, 1, 1, &a, VALUES_INT);
}

GLAPI void APIENTRY glUniform2ui(GLint l, GLuint a, GLuint b)
{
    GLuint v[2] = {a, b};
    set_uniform(l, 1, 2, v, VALUES_INT);
}

GLAPI void APIENTRY glUniform3ui(GLint l, GLuint a, GLuint b, GLuint c)
{
    GLuint v[3] = {a, b, c};
    set_uniform(l, 1, 3, v, VALUES_INT);
}

GLAPI void APIENTRY glUniform4ui(GLint l, GLuint a, GLuint b, GLuint c, GLuint d)
{
    GLuint v[4] = {a, b, c, d};
    set_uniform(l, 1, 4, v, VALUES_INT);
}

GLAPI void APIENTRY glUniform1d(GLint l, GLdouble a)
{
    set_uniform(l, 1, 1, &a, VALUES_DOUBLE);
}

GLAPI void APIENTRY glUniform2d(GLint l, GLdouble a, GLdouble b)
{
    GLdouble v[2] = {a, b};
    set_uniform(l, 1, 2, v, VALUES_DOUBLE);
}

GLAPI void APIENTRY glUniform3d(GLint l, GLdouble a, GLdouble b, GLdouble c)
{
    GLdouble v[3] = {a, b, c};
    set_uniform(l, 1, 3, v, VALUES_DOUBLE);
}

GLAPI void APIENTRY glUniform4d(GLint l, GLdouble a, GLdouble b, GLdouble c, GLdouble d)
{
    GLdouble v[4] = {a, b, c, d};
    set_uniform(l, 1, 4, v, VALUES_DOUBLE);
}

GLAPI void APIENTRY glUniform1fv(GLint l, GLsizei n, const GLfloat *v)
{
    set_uniform(l, n, 1, v, VALUES_FLOAT);
}

GLAPI void APIENTRY glUniform2fv(GLint l, GLsizei n, const GLfloat *v)
{
    set_uniform(l, n, 2, v, VALUES_FLOAT);
}

GLAPI void APIENTRY glUniform3fv(GLint l, GLsizei n, const GLfloat *v)
{
    set_uniform(l, n, 3, v, VALUES_FLOAT);
}

GLAPI void APIENTRY glUniform4fv(GLint l, GLsizei n, const GLfloat *v)
{
    set_uniform(l, n, 4, v, VALUES_FLOAT);
}

GLAPI void APIENTRY glUniform1iv(GLint l, GLsizei n, const GLint *v)
{
    set_uniform(l, n, 1, v, VALUES_INT);
}

GLAPI void APIENTRY glUniform2iv(GLint l, GLsizei n, const GLint *v)
{
    set_uniform(l, n, 2, v, VALUES_INT);
}

GLAPI void APIENTRY glUniform3iv(GLint l, GLsizei n, const GLint *v)
{
    set_uniform(l, n, 3, v, VALUES_INT);
}

GLAPI void APIENTRY glUniform4iv(GLint l, GLsizei n, const GLint *v)
{
    set_uniform(l, n, 4, v, VALUES_INT);
}

GLAPI void APIENTRY glUniform1uiv(GLint l, GLsizei n, const GLuint *v)
{
    set_uniform(l, n, 1, v, VALUES_INT);
}

GLAPI void APIENTRY glUniform2uiv(GLint l, GLsizei n, const GLuint *v)
{
    set_uniform(l, n, 2, v, VALUES_INT);
}

GLAPI void APIENTRY glUniform3uiv(GLint l, GLsizei n, const GLuint *v)
{
    set_uniform(l, n, 3, v, VALUES_INT);
}

GLAPI void APIENTRY glUniform4uiv(GLint l, GLsizei n, const GLuint *v)
{
    set_uniform(l, n, 4, v, VALUES_INT);
}

GLAPI void APIENTRY glUniform1dv(GLint l, GLsizei n, const GLdouble *v)
{
    set_uniform(l, n, 1, v, VALUES_DOUBLE);
}

GLAPI void APIENTRY glUniform2dv(GLint l, GLsizei n, const GLdouble *v)
{
    set_uniform(l, n, 2, v, VALUES_DOUBLE);
}

GLAPI void APIENTRY glUniform3dv(GLint l, GLsizei n, const GLdouble *v)
{
    set_uniform(l, n, 3, v, VALUES_DOUBLE);
}

GLAPI void APIENTRY glUniform4dv(GLint l, GLsizei n, const GLdouble *v)
{
    set_uniform(l, n, 4, v, VALUES_DOUBLE);
}

GLAPI void APIENTRY glUniformMatrix2fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    set_matrix(l, n, 2, 2, t, v);
}

GLAPI void APIENTRY glUniformMatrix3fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    set_matrix(l, n, 3, 3, t, v);
}

GLAPI void APIENTRY glUniformMatrix4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    set_matrix(l, n, 4, 4, t, v);
}

GLAPI void APIENTRY glUniformMatrix2x3fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    set_matrix(l, n, 2, 3, t, v);
}

GLAPI void APIENTRY glUniformMatrix3x2fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    set_matrix(l, n, 3, 2, t, v);
}

GLAPI void APIENTRY glUniformMatrix2x4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    set_matrix(l, n, 2, 4, t, v);
}

GLAPI void APIENTRY glUniformMatrix4x2fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    set_matrix(l, n, 4, 2, t, v);
}

GLAPI void APIENTRY glUniformMatrix3x4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    set_matrix(l, n, 3, 4, t, v);
}

GLAPI void APIENTRY glUniformMatrix4x3fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{
    set_matrix(l, n, 4, 3, t, v);
}

GLAPI void APIENTRY glUniformMatrix2dv(GLint l, GLsizei n, GLboolean t, const GLdouble *v)
{
    set_matrix_d(l, n, 2, 2, t, v);
}

GLAPI void APIENTRY glUniformMatrix3dv(GLint l, GLsizei n, GLboolean t, const GLdouble *v)
{
    set_matrix_d(l, n, 3, 3, t, v);
}

GLAPI void APIENTRY glUniformMatrix4dv(GLint l, GLsizei n, GLboolean t, const GLdouble *v)
{
    set_matrix_d(l, n, 4, 4, t, v);
}

GLAPI void APIENTRY glUniformMatrix2x3dv(GLint l, GLsizei n, GLboolean t, const GLdouble *v)
{
    set_matrix_d(l, n, 2, 3, t, v);
}

GLAPI void APIENTRY glUniformMatrix3x2dv(GLint l, GLsizei n, GLboolean t, const GLdouble *v)
{
    set_matrix_d(l, n, 3, 2, t, v);
}

GLAPI void APIENTRY glUniformMatrix2x4dv(GLint l, GLsizei n, GLboolean t, const GLdouble *v)
{
    set_matrix_d(l, n, 2, 4, t, v);
}

GLAPI void APIENTRY glUniformMatrix4x2dv(GLint l, GLsizei n, GLboolean t, const GLdouble *v)
{
    set_matrix_d(l, n, 4, 2, t, v);
}

GLAPI void APIENTRY glUniformMatrix3x4dv(GLint l, GLsizei n, GLboolean t, const GLdouble *v)
{
    set_matrix_d(l, n, 3, 4, t, v);
}

GLAPI void APIENTRY glUniformMatrix4x3dv(GLint l, GLsizei n, GLboolean t, const GLdouble *v)
{
    set_matrix_d(l, n, 4, 3, t, v);
}

/* reads one element as doubles, returns the component count. */
static int get_uniform(GLuint name, GLint location, double *params)
{
    GL_CONTEXT_OR_RETURN(0);
    GlProgram *p = gl_program(ctx, name);
    GLint element;
    GlUniform *u = lookup(ctx, p, location, &element);
    if (!u)
        return 0;
    if (u->units) {
        params[0] = (double)u->units[element];
        return 1;
    }
    int cols, rows, class;
    shape(u->type, &cols, &rows, &class);
    uint32_t size = class == 2 ? 8 : 4;
    uint32_t stride = u->array_stride ? u->array_stride : (uint32_t)(rows * cols) * size;
    uint32_t mstride = u->matrix_stride ? u->matrix_stride : (uint32_t)rows * size;
    const uint8_t *src = p->ublock + u->offset + (uint32_t)element * stride;
    for (int c = 0; c < cols; c++)
        for (int r = 0; r < rows; r++) {
            const uint8_t *s = src + (uint32_t)c * mstride + (uint32_t)r * size;
            double v;
            if (class == 2) {
                memcpy(&v, s, 8);
            } else if (class == 1) {
                float f;
                memcpy(&f, s, 4);
                v = f;
            } else {
                GLint i;
                memcpy(&i, s, 4);
                v = i;
            }
            params[c * rows + r] = v;
        }
    return cols * rows;
}

GLAPI void APIENTRY glGetUniformfv(GLuint name, GLint location, GLfloat *params)
{
    double v[16];
    int n = get_uniform(name, location, v);
    for (int i = 0; i < n; i++)
        params[i] = (GLfloat)v[i];
}

GLAPI void APIENTRY glGetUniformiv(GLuint name, GLint location, GLint *params)
{
    double v[16];
    int n = get_uniform(name, location, v);
    for (int i = 0; i < n; i++)
        params[i] = (GLint)v[i];
}

GLAPI void APIENTRY glGetUniformuiv(GLuint name, GLint location, GLuint *params)
{
    double v[16];
    int n = get_uniform(name, location, v);
    for (int i = 0; i < n; i++)
        params[i] = (GLuint)(GLint)v[i];
}

GLAPI void APIENTRY glGetUniformdv(GLuint name, GLint location, GLdouble *params)
{
    double v[16];
    int n = get_uniform(name, location, v);
    for (int i = 0; i < n; i++)
        params[i] = v[i];
}

/* ---------------------------------------------------------------- the same on a program of a name */

/* the program a glProgramUniform call names, or null with the error raised. */
static GlProgram *named_program(GlContext *ctx, GLuint program)
{
    GlProgram *p = gl_program(ctx, program);
    if (!p)
        gl_error(ctx, GL_INVALID_VALUE);
    return p;
}

GLAPI void APIENTRY glProgramUniform1f(GLuint program, GLint l, GLfloat a)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    GLfloat v[1] = {a};
    if (p)
        put_uniform(ctx, p, l, 1, 1, v, VALUES_FLOAT);
}

GLAPI void APIENTRY glProgramUniform1fv(GLuint program, GLint l, GLsizei count, const GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_uniform(ctx, p, l, count, 1, v, VALUES_FLOAT);
}

GLAPI void APIENTRY glProgramUniform1i(GLuint program, GLint l, GLint a)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    GLint v[1] = {a};
    if (p)
        put_uniform(ctx, p, l, 1, 1, v, VALUES_INT);
}

GLAPI void APIENTRY glProgramUniform1iv(GLuint program, GLint l, GLsizei count, const GLint *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_uniform(ctx, p, l, count, 1, v, VALUES_INT);
}

GLAPI void APIENTRY glProgramUniform1ui(GLuint program, GLint l, GLuint a)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    GLuint v[1] = {a};
    if (p)
        put_uniform(ctx, p, l, 1, 1, v, VALUES_INT);
}

GLAPI void APIENTRY glProgramUniform1uiv(GLuint program, GLint l, GLsizei count, const GLuint *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_uniform(ctx, p, l, count, 1, v, VALUES_INT);
}

GLAPI void APIENTRY glProgramUniform1d(GLuint program, GLint l, GLdouble a)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    GLdouble v[1] = {a};
    if (p)
        put_uniform(ctx, p, l, 1, 1, v, VALUES_DOUBLE);
}

GLAPI void APIENTRY glProgramUniform1dv(GLuint program, GLint l, GLsizei count, const GLdouble *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_uniform(ctx, p, l, count, 1, v, VALUES_DOUBLE);
}

GLAPI void APIENTRY glProgramUniform2f(GLuint program, GLint l, GLfloat a, GLfloat b)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    GLfloat v[2] = {a, b};
    if (p)
        put_uniform(ctx, p, l, 1, 2, v, VALUES_FLOAT);
}

GLAPI void APIENTRY glProgramUniform2fv(GLuint program, GLint l, GLsizei count, const GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_uniform(ctx, p, l, count, 2, v, VALUES_FLOAT);
}

GLAPI void APIENTRY glProgramUniform2i(GLuint program, GLint l, GLint a, GLint b)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    GLint v[2] = {a, b};
    if (p)
        put_uniform(ctx, p, l, 1, 2, v, VALUES_INT);
}

GLAPI void APIENTRY glProgramUniform2iv(GLuint program, GLint l, GLsizei count, const GLint *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_uniform(ctx, p, l, count, 2, v, VALUES_INT);
}

GLAPI void APIENTRY glProgramUniform2ui(GLuint program, GLint l, GLuint a, GLuint b)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    GLuint v[2] = {a, b};
    if (p)
        put_uniform(ctx, p, l, 1, 2, v, VALUES_INT);
}

GLAPI void APIENTRY glProgramUniform2uiv(GLuint program, GLint l, GLsizei count, const GLuint *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_uniform(ctx, p, l, count, 2, v, VALUES_INT);
}

GLAPI void APIENTRY glProgramUniform2d(GLuint program, GLint l, GLdouble a, GLdouble b)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    GLdouble v[2] = {a, b};
    if (p)
        put_uniform(ctx, p, l, 1, 2, v, VALUES_DOUBLE);
}

GLAPI void APIENTRY glProgramUniform2dv(GLuint program, GLint l, GLsizei count, const GLdouble *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_uniform(ctx, p, l, count, 2, v, VALUES_DOUBLE);
}

GLAPI void APIENTRY glProgramUniform3f(GLuint program, GLint l, GLfloat a, GLfloat b, GLfloat c)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    GLfloat v[3] = {a, b, c};
    if (p)
        put_uniform(ctx, p, l, 1, 3, v, VALUES_FLOAT);
}

GLAPI void APIENTRY glProgramUniform3fv(GLuint program, GLint l, GLsizei count, const GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_uniform(ctx, p, l, count, 3, v, VALUES_FLOAT);
}

GLAPI void APIENTRY glProgramUniform3i(GLuint program, GLint l, GLint a, GLint b, GLint c)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    GLint v[3] = {a, b, c};
    if (p)
        put_uniform(ctx, p, l, 1, 3, v, VALUES_INT);
}

GLAPI void APIENTRY glProgramUniform3iv(GLuint program, GLint l, GLsizei count, const GLint *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_uniform(ctx, p, l, count, 3, v, VALUES_INT);
}

GLAPI void APIENTRY glProgramUniform3ui(GLuint program, GLint l, GLuint a, GLuint b, GLuint c)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    GLuint v[3] = {a, b, c};
    if (p)
        put_uniform(ctx, p, l, 1, 3, v, VALUES_INT);
}

GLAPI void APIENTRY glProgramUniform3uiv(GLuint program, GLint l, GLsizei count, const GLuint *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_uniform(ctx, p, l, count, 3, v, VALUES_INT);
}

GLAPI void APIENTRY glProgramUniform3d(GLuint program, GLint l, GLdouble a, GLdouble b, GLdouble c)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    GLdouble v[3] = {a, b, c};
    if (p)
        put_uniform(ctx, p, l, 1, 3, v, VALUES_DOUBLE);
}

GLAPI void APIENTRY glProgramUniform3dv(GLuint program, GLint l, GLsizei count, const GLdouble *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_uniform(ctx, p, l, count, 3, v, VALUES_DOUBLE);
}

GLAPI void APIENTRY glProgramUniform4f(GLuint program, GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    GLfloat v[4] = {a, b, c, d};
    if (p)
        put_uniform(ctx, p, l, 1, 4, v, VALUES_FLOAT);
}

GLAPI void APIENTRY glProgramUniform4fv(GLuint program, GLint l, GLsizei count, const GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_uniform(ctx, p, l, count, 4, v, VALUES_FLOAT);
}

GLAPI void APIENTRY glProgramUniform4i(GLuint program, GLint l, GLint a, GLint b, GLint c, GLint d)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    GLint v[4] = {a, b, c, d};
    if (p)
        put_uniform(ctx, p, l, 1, 4, v, VALUES_INT);
}

GLAPI void APIENTRY glProgramUniform4iv(GLuint program, GLint l, GLsizei count, const GLint *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_uniform(ctx, p, l, count, 4, v, VALUES_INT);
}

GLAPI void APIENTRY glProgramUniform4ui(GLuint program, GLint l, GLuint a, GLuint b, GLuint c, GLuint d)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    GLuint v[4] = {a, b, c, d};
    if (p)
        put_uniform(ctx, p, l, 1, 4, v, VALUES_INT);
}

GLAPI void APIENTRY glProgramUniform4uiv(GLuint program, GLint l, GLsizei count, const GLuint *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_uniform(ctx, p, l, count, 4, v, VALUES_INT);
}

GLAPI void APIENTRY glProgramUniform4d(GLuint program, GLint l, GLdouble a, GLdouble b, GLdouble c, GLdouble d)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    GLdouble v[4] = {a, b, c, d};
    if (p)
        put_uniform(ctx, p, l, 1, 4, v, VALUES_DOUBLE);
}

GLAPI void APIENTRY glProgramUniform4dv(GLuint program, GLint l, GLsizei count, const GLdouble *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_uniform(ctx, p, l, count, 4, v, VALUES_DOUBLE);
}

GLAPI void APIENTRY glProgramUniformMatrix2fv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 2, 2, t, v, FALSE);
}

GLAPI void APIENTRY glProgramUniformMatrix3fv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 3, 3, t, v, FALSE);
}

GLAPI void APIENTRY glProgramUniformMatrix4fv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 4, 4, t, v, FALSE);
}

GLAPI void APIENTRY glProgramUniformMatrix2x3fv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 2, 3, t, v, FALSE);
}

GLAPI void APIENTRY glProgramUniformMatrix3x2fv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 3, 2, t, v, FALSE);
}

GLAPI void APIENTRY glProgramUniformMatrix2x4fv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 2, 4, t, v, FALSE);
}

GLAPI void APIENTRY glProgramUniformMatrix4x2fv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 4, 2, t, v, FALSE);
}

GLAPI void APIENTRY glProgramUniformMatrix3x4fv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 3, 4, t, v, FALSE);
}

GLAPI void APIENTRY glProgramUniformMatrix4x3fv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 4, 3, t, v, FALSE);
}

GLAPI void APIENTRY glProgramUniformMatrix2dv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLdouble *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 2, 2, t, v, TRUE);
}

GLAPI void APIENTRY glProgramUniformMatrix3dv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLdouble *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 3, 3, t, v, TRUE);
}

GLAPI void APIENTRY glProgramUniformMatrix4dv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLdouble *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 4, 4, t, v, TRUE);
}

GLAPI void APIENTRY glProgramUniformMatrix2x3dv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLdouble *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 2, 3, t, v, TRUE);
}

GLAPI void APIENTRY glProgramUniformMatrix3x2dv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLdouble *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 3, 2, t, v, TRUE);
}

GLAPI void APIENTRY glProgramUniformMatrix2x4dv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLdouble *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 2, 4, t, v, TRUE);
}

GLAPI void APIENTRY glProgramUniformMatrix4x2dv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLdouble *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 4, 2, t, v, TRUE);
}

GLAPI void APIENTRY glProgramUniformMatrix3x4dv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLdouble *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 3, 4, t, v, TRUE);
}

GLAPI void APIENTRY glProgramUniformMatrix4x3dv(GLuint program, GLint l, GLsizei count, GLboolean t, const GLdouble *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = named_program(ctx, program);
    if (p)
        put_matrix(ctx, p, l, count, 4, 3, t, v, TRUE);
}

/* ---------------------------------------------------------------- program binaries */

/*
 * the binary of a program is its shader sources and the bindings made before the link, in a format of this driver: a
 * program loaded from it links again on the host. a binary kept from another driver version or another driver does not load.
 */
#define PROGRAM_BINARY_FORMAT 0x4D494D31
static const char g_binary_magic[8] = {'M', 'I', 'M', 'E', 'P', 'B', '1', 0};

static SIZE_T binary_size(const GlProgram *p)
{
    SIZE_T n = 8 + 4 + 4 + p->bindings_len + 4 + p->frag_bindings_len;
    for (uint32_t i = 0; i < p->shader_count; i++)
        n += 8 + (p->shaders[i]->source ? strlen(p->shaders[i]->source) : 0);
    return n;
}

static uint8_t *binary_put(uint8_t *at, const void *data, SIZE_T size)
{
    if (size)
        memcpy(at, data, size);
    return at + size;
}

GLAPI void APIENTRY glGetProgramBinary(GLuint name, GLsizei size, GLsizei *length, GLenum *format, void *binary)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, name);
    if (!p) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    gl_program_ready(p);
    SIZE_T need = binary_size(p);
    if (!p->link_status || size < 0 || (SIZE_T)size < need) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    uint8_t *at = binary;
    at = binary_put(at, g_binary_magic, 8);
    uint32_t v = p->shader_count;
    at = binary_put(at, &v, 4);
    for (uint32_t i = 0; i < p->shader_count; i++) {
        const char *source = p->shaders[i]->source ? p->shaders[i]->source : "";
        uint32_t head[2] = {p->shaders[i]->type, (uint32_t)strlen(source)};
        at = binary_put(at, head, 8);
        at = binary_put(at, source, head[1]);
    }
    v = p->bindings_len;
    at = binary_put(at, &v, 4);
    at = binary_put(at, p->bindings, p->bindings_len);
    v = p->frag_bindings_len;
    at = binary_put(at, &v, 4);
    at = binary_put(at, p->frag_bindings, p->frag_bindings_len);
    if (length)
        *length = (GLsizei)need;
    if (format)
        *format = PROGRAM_BINARY_FORMAT;
}

GLAPI void APIENTRY glProgramBinary(GLuint name, GLenum format, const void *binary, GLsizei size)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, name);
    if (!p) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (format != PROGRAM_BINARY_FORMAT) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    const uint8_t *at = binary, *end = at + (size > 0 ? size : 0);
    uint32_t count = 0;
    BOOL ok = size >= 12 && !memcmp(at, g_binary_magic, 8);
    if (ok) {
        memcpy(&count, at + 8, 4);
        at += 12;
        ok = count >= 1 && count <= 8;
    }
    GlShader *loaded[8] = {};
    for (uint32_t i = 0; ok && i < count; i++) {
        uint32_t head[2];
        ok = end - at >= 8;
        if (!ok)
            break;
        memcpy(head, at, 8);
        at += 8;
        ok = (SIZE_T)(end - at) >= head[1];
        if (!ok)
            break;
        GlShader *s = gl_alloc(sizeof(GlShader));
        if (!s) {
            ok = FALSE;
            break;
        }
        s->type = head[0];
        s->refs = 1;
        s->deleted = TRUE;
        s->source = gl_alloc(head[1] + 1);
        if (s->source)
            memcpy(s->source, at, head[1]);
        loaded[i] = s;
        at += head[1];
    }
    char *tables[2] = {};
    uint32_t table_len[2] = {};
    for (int t = 0; ok && t < 2; t++) {
        ok = end - at >= 4;
        if (!ok)
            break;
        memcpy(&table_len[t], at, 4);
        at += 4;
        ok = (SIZE_T)(end - at) >= table_len[t];
        if (!ok)
            break;
        if (table_len[t]) {
            tables[t] = gl_alloc(table_len[t]);
            if (tables[t])
                memcpy(tables[t], at, table_len[t]);
        }
        at += table_len[t];
    }
    if (!ok) {
        for (uint32_t i = 0; i < 8; i++)
            if (loaded[i]) {
                gl_free(loaded[i]->source);
                gl_free(loaded[i]);
            }
        gl_free(tables[0]);
        gl_free(tables[1]);
        gl_program_ready(p);
        free_reflection(p);
        p->linked = TRUE;
        p->link_status = FALSE;
        gl_free(p->log);
        p->log = gl_strdup("the program binary is not one this driver made\n");
        return;
    }
    while (p->shader_count)
        release_shader(ctx, p->shaders[--p->shader_count]);
    for (uint32_t i = 0; i < count; i++)
        p->shaders[p->shader_count++] = loaded[i];
    gl_free(p->bindings);
    gl_free(p->frag_bindings);
    p->bindings = tables[0];
    p->bindings_len = table_len[0];
    p->frag_bindings = tables[1];
    p->frag_bindings_len = table_len[1];
    glLinkProgram(name);
}

/* ---------------------------------------------------------------- uniform names and indices */

GLAPI void APIENTRY glGetUniformIndices(GLuint name, GLsizei count, const GLchar *const *names, GLuint *indices)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, name);
    if (!p || !gl_program_ready(p) || count < 0) {
        gl_error(ctx, p ? GL_INVALID_VALUE : GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < count; i++) {
        indices[i] = GL_INVALID_INDEX;
        SIZE_T n = strlen(names[i]);
        for (uint32_t k = 0; k < p->uniform_count; k++) {
            const GlUniform *u = &p->uniforms[k];
            if (!strcmp(u->name, names[i]) || (u->base_len == n && !strncmp(u->name, names[i], n))) {
                indices[i] = k;
                break;
            }
        }
    }
}

GLAPI void APIENTRY glGetActiveUniformName(GLuint name, GLuint index, GLsizei size, GLsizei *length, GLchar *out)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, name);
    if (!p || !gl_program_ready(p) || index >= p->uniform_count || size < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    copy_name(p->uniforms[index].name, size, length, out);
}

/* this driver has no binary format of shaders: GL_NUM_SHADER_BINARY_FORMATS is 0, and every format is refused. */
GLAPI void APIENTRY glShaderBinary(GLsizei count, const GLuint *shaders, GLenum format, const void *binary, GLsizei size)
{
    GL_CONTEXT_OR_RETURN();
    gl_error(ctx, GL_INVALID_ENUM);
}
