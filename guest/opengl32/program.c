/*
 * shaders and programs. sources are kept until a link, when both stages go to the host compiler; the link is
 * asynchronous and only a query or a draw waits for it. uniforms live in a block laid out by the host's reflection,
 * written here and sent with the draws that need it. a location is the uniform's index times 4096 plus the array
 * element.
 */
#include "opengl32.h"

#define INFO_CAPACITY (128u << 10)

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
    if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER) {
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
    gl_free(p->uniforms);
    gl_free(p->attribs);
    gl_free(p->ublock);
    gl_free(p->log);
    p->uniforms = nullptr;
    p->attribs = nullptr;
    p->ublock = nullptr;
    p->log = nullptr;
    p->uniform_count = p->attrib_count = 0;
    p->attrib_mask = 0;
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

GLAPI void APIENTRY glBindFragDataLocation(GLuint program, GLuint color, const GLchar *name)
{
}

/* sends both stages to the host compiler; the program is usable once gl_program_ready says so. */
BOOL gl_program_link_sources(GlProgram *p, const char *vs, const char *fs)
{
    static uint32_t link_serial;
    gl_program_ready(p);
    free_reflection(p);
    p->linked = p->link_status = FALSE;
    gl_device_lock();
    p->serial = ++link_serial;
    gl_device_unlock();
    if (!vs || !fs) {
        p->log = gl_strdup(vs ? "no fragment shader\n" : "no vertex shader\n");
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
    uint32_t vn = (uint32_t)strlen(vs), fn = (uint32_t)strlen(fs);
    gl_device_lock();
    MimeGpuProgramCreate *c =
        gl_emit(MIME_GPU_PROGRAM_CREATE, (uint32_t)sizeof(MimeGpuProgramCreate) + vn + fn + p->bindings_len);
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

GLAPI void APIENTRY glLinkProgram(GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, name);
    if (!p) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    const char *vs = nullptr, *fs = nullptr;
    for (uint32_t i = 0; i < p->shader_count; i++) {
        GlShader *s = p->shaders[i];
        if (s->type == GL_VERTEX_SHADER && !vs)
            vs = s->source;
        else if (s->type == GL_FRAGMENT_SHADER && !fs)
            fs = s->source;
    }
    gl_program_link_sources(p, vs, fs);
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
        GlAttrib *x = &p->attribs[p->attrib_count++];
        x->name = gl_strdup((const char *)base + a[i].name_offset);
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
        if (x->texture[0] >= 0 || x->texture[1] >= 0)
            x->units = gl_alloc((SIZE_T)x->size * sizeof(GLint));
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
        *params = 0;
        break;
    default:
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
    case GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER: *params = (block->stages & 1) != 0; break;
    case GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER: *params = (block->stages & 2) != 0; break;
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

/* columns and rows of a uniform type; scalars and vectors have one column. */
static void shape(GLenum type, int *cols, int *rows, BOOL *is_float)
{
    *cols = 1;
    *is_float = TRUE;
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
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_BOOL:
        *rows = 1;
        *is_float = FALSE;
        break;
    case GL_INT_VEC2:
    case GL_UNSIGNED_INT_VEC2:
    case GL_BOOL_VEC2:
        *rows = 2;
        *is_float = FALSE;
        break;
    case GL_INT_VEC3:
    case GL_UNSIGNED_INT_VEC3:
    case GL_BOOL_VEC3:
        *rows = 3;
        *is_float = FALSE;
        break;
    case GL_INT_VEC4:
    case GL_UNSIGNED_INT_VEC4:
    case GL_BOOL_VEC4:
        *rows = 4;
        *is_float = FALSE;
        break;
    case GL_FLOAT_MAT2:
        *cols = *rows = 2;
        break;
    case GL_FLOAT_MAT3:
        *cols = *rows = 3;
        break;
    case GL_FLOAT_MAT4:
        *cols = *rows = 4;
        break;
    case GL_FLOAT_MAT2x3:
        *cols = 2;
        *rows = 3;
        break;
    case GL_FLOAT_MAT2x4:
        *cols = 2;
        *rows = 4;
        break;
    case GL_FLOAT_MAT3x2:
        *cols = 3;
        *rows = 2;
        break;
    case GL_FLOAT_MAT3x4:
        *cols = 3;
        *rows = 4;
        break;
    case GL_FLOAT_MAT4x2:
        *cols = 4;
        *rows = 2;
        break;
    case GL_FLOAT_MAT4x3:
        *cols = 4;
        *rows = 3;
        break;
    default:
        *rows = 1;
        *is_float = FALSE;
        break;
    }
}

/* writes count elements of n components each, floats or ints as given; samplers take units. */
static void set_uniform(GLint location, GLsizei count, int n, const void *values, BOOL floats)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = ctx->current_program;
    GLint element;
    GlUniform *u = lookup(ctx, p, location, &element);
    if (!u)
        return;
    if (count > u->size - element)
        count = u->size - element;
    if (u->units) {
        for (GLsizei i = 0; i < count; i++)
            u->units[element + i] = floats ? (GLint)((const GLfloat *)values)[i] : ((const GLint *)values)[i];
        ctx->dirty |= DIRTY_TEXTURES;
        return;
    }
    int cols, rows;
    BOOL is_float;
    shape(u->type, &cols, &rows, &is_float);
    uint32_t stride = u->array_stride ? u->array_stride : (uint32_t)rows * 4;
    for (GLsizei i = 0; i < count; i++) {
        uint8_t *dst = p->ublock + u->offset + (uint32_t)(element + i) * stride;
        if (dst + (uint32_t)n * 4 > p->ublock + p->ublock_size)
            break;
        for (int c = 0; c < n; c++) {
            if (floats == is_float || !is_float) {
                if (floats && !is_float) {
                    GLint v = (GLint)((const GLfloat *)values)[i * n + c];
                    memcpy(dst + c * 4, &v, 4);
                } else {
                    memcpy(dst + c * 4, (const uint8_t *)values + ((SIZE_T)i * (SIZE_T)n + (SIZE_T)c) * 4, 4);
                }
            } else {
                GLfloat v = (GLfloat)((const GLint *)values)[i * n + c];
                memcpy(dst + c * 4, &v, 4);
            }
        }
    }
    p->ublock_dirty = TRUE;
    ctx->dirty |= DIRTY_UNIFORMS;
}

static void set_matrix(GLint location, GLsizei count, int cols, int rows, GLboolean transpose, const GLfloat *v)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = ctx->current_program;
    GLint element;
    GlUniform *u = lookup(ctx, p, location, &element);
    if (!u)
        return;
    if (count > u->size - element)
        count = u->size - element;
    uint32_t mstride = u->matrix_stride ? u->matrix_stride : (uint32_t)rows * 4;
    uint32_t astride = u->array_stride ? u->array_stride : mstride * (uint32_t)cols;
    for (GLsizei i = 0; i < count; i++) {
        const GLfloat *m = v + (SIZE_T)i * (SIZE_T)(cols * rows);
        uint8_t *dst = p->ublock + u->offset + (uint32_t)(element + i) * astride;
        if (dst + mstride * (uint32_t)cols > p->ublock + p->ublock_size + 16)
            break;
        for (int c = 0; c < cols; c++)
            for (int r = 0; r < rows; r++) {
                GLfloat x = transpose ? m[r * cols + c] : m[c * rows + r];
                memcpy(dst + (uint32_t)c * mstride + (uint32_t)r * 4, &x, 4);
            }
    }
    p->ublock_dirty = TRUE;
    ctx->dirty |= DIRTY_UNIFORMS;
}

GLAPI void APIENTRY glUniform1f(GLint l, GLfloat a)
{
    set_uniform(l, 1, 1, &a, TRUE);
}

GLAPI void APIENTRY glUniform2f(GLint l, GLfloat a, GLfloat b)
{
    GLfloat v[2] = {a, b};
    set_uniform(l, 1, 2, v, TRUE);
}

GLAPI void APIENTRY glUniform3f(GLint l, GLfloat a, GLfloat b, GLfloat c)
{
    GLfloat v[3] = {a, b, c};
    set_uniform(l, 1, 3, v, TRUE);
}

GLAPI void APIENTRY glUniform4f(GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d)
{
    GLfloat v[4] = {a, b, c, d};
    set_uniform(l, 1, 4, v, TRUE);
}

GLAPI void APIENTRY glUniform1i(GLint l, GLint a)
{
    set_uniform(l, 1, 1, &a, FALSE);
}

GLAPI void APIENTRY glUniform2i(GLint l, GLint a, GLint b)
{
    GLint v[2] = {a, b};
    set_uniform(l, 1, 2, v, FALSE);
}

GLAPI void APIENTRY glUniform3i(GLint l, GLint a, GLint b, GLint c)
{
    GLint v[3] = {a, b, c};
    set_uniform(l, 1, 3, v, FALSE);
}

GLAPI void APIENTRY glUniform4i(GLint l, GLint a, GLint b, GLint c, GLint d)
{
    GLint v[4] = {a, b, c, d};
    set_uniform(l, 1, 4, v, FALSE);
}

GLAPI void APIENTRY glUniform1ui(GLint l, GLuint a)
{
    set_uniform(l, 1, 1, &a, FALSE);
}

GLAPI void APIENTRY glUniform2ui(GLint l, GLuint a, GLuint b)
{
    GLuint v[2] = {a, b};
    set_uniform(l, 1, 2, v, FALSE);
}

GLAPI void APIENTRY glUniform3ui(GLint l, GLuint a, GLuint b, GLuint c)
{
    GLuint v[3] = {a, b, c};
    set_uniform(l, 1, 3, v, FALSE);
}

GLAPI void APIENTRY glUniform4ui(GLint l, GLuint a, GLuint b, GLuint c, GLuint d)
{
    GLuint v[4] = {a, b, c, d};
    set_uniform(l, 1, 4, v, FALSE);
}

GLAPI void APIENTRY glUniform1fv(GLint l, GLsizei n, const GLfloat *v)
{
    set_uniform(l, n, 1, v, TRUE);
}

GLAPI void APIENTRY glUniform2fv(GLint l, GLsizei n, const GLfloat *v)
{
    set_uniform(l, n, 2, v, TRUE);
}

GLAPI void APIENTRY glUniform3fv(GLint l, GLsizei n, const GLfloat *v)
{
    set_uniform(l, n, 3, v, TRUE);
}

GLAPI void APIENTRY glUniform4fv(GLint l, GLsizei n, const GLfloat *v)
{
    set_uniform(l, n, 4, v, TRUE);
}

GLAPI void APIENTRY glUniform1iv(GLint l, GLsizei n, const GLint *v)
{
    set_uniform(l, n, 1, v, FALSE);
}

GLAPI void APIENTRY glUniform2iv(GLint l, GLsizei n, const GLint *v)
{
    set_uniform(l, n, 2, v, FALSE);
}

GLAPI void APIENTRY glUniform3iv(GLint l, GLsizei n, const GLint *v)
{
    set_uniform(l, n, 3, v, FALSE);
}

GLAPI void APIENTRY glUniform4iv(GLint l, GLsizei n, const GLint *v)
{
    set_uniform(l, n, 4, v, FALSE);
}

GLAPI void APIENTRY glUniform1uiv(GLint l, GLsizei n, const GLuint *v)
{
    set_uniform(l, n, 1, v, FALSE);
}

GLAPI void APIENTRY glUniform2uiv(GLint l, GLsizei n, const GLuint *v)
{
    set_uniform(l, n, 2, v, FALSE);
}

GLAPI void APIENTRY glUniform3uiv(GLint l, GLsizei n, const GLuint *v)
{
    set_uniform(l, n, 3, v, FALSE);
}

GLAPI void APIENTRY glUniform4uiv(GLint l, GLsizei n, const GLuint *v)
{
    set_uniform(l, n, 4, v, FALSE);
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

/* reads one element as floats, returns the component count. */
static int get_uniform(GLuint name, GLint location, GLfloat *params)
{
    GL_CONTEXT_OR_RETURN(0);
    GlProgram *p = gl_program(ctx, name);
    GLint element;
    GlUniform *u = lookup(ctx, p, location, &element);
    if (!u)
        return 0;
    if (u->units) {
        params[0] = (GLfloat)u->units[element];
        return 1;
    }
    int cols, rows;
    BOOL is_float;
    shape(u->type, &cols, &rows, &is_float);
    uint32_t stride = u->array_stride ? u->array_stride : (uint32_t)(rows * cols) * 4;
    uint32_t mstride = u->matrix_stride ? u->matrix_stride : (uint32_t)rows * 4;
    const uint8_t *src = p->ublock + u->offset + (uint32_t)element * stride;
    for (int c = 0; c < cols; c++)
        for (int r = 0; r < rows; r++) {
            const uint8_t *s = src + (uint32_t)c * mstride + (uint32_t)r * 4;
            if (is_float) {
                memcpy(&params[c * rows + r], s, 4);
            } else {
                GLint v;
                memcpy(&v, s, 4);
                params[c * rows + r] = (GLfloat)v;
            }
        }
    return cols * rows;
}

GLAPI void APIENTRY glGetUniformfv(GLuint name, GLint location, GLfloat *params)
{
    GLfloat v[16];
    int n = get_uniform(name, location, v);
    memcpy(params, v, (SIZE_T)n * sizeof(GLfloat));
}

GLAPI void APIENTRY glGetUniformiv(GLuint name, GLint location, GLint *params)
{
    GLfloat v[16];
    int n = get_uniform(name, location, v);
    for (int i = 0; i < n; i++)
        params[i] = (GLint)v[i];
}
