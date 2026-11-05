/*
 * buffer objects live in heap memory the gpu reads in place, so updating a buffer the gpu may still read moves it to
 * new memory instead of waiting. the old memory is reused once the gpu passed the last fence that read it.
 */
#include "opengl32.h"


GlBuffer *gl_buffer(GlContext *ctx, GLuint name)
{
    void *b = gl_name_get(&ctx->shared->buffers, name);
    return b == GL_RESERVED ? nullptr : b;
}

static GLuint *binding_of(GlContext *ctx, GLenum target)
{
    switch (target) {
    case GL_ARRAY_BUFFER:
        return &ctx->array_buffer;
    case GL_ELEMENT_ARRAY_BUFFER:
        return &ctx->vao->element_buffer;
    case GL_PIXEL_PACK_BUFFER:
        return &ctx->pixel_pack;
    case GL_PIXEL_UNPACK_BUFFER:
        return &ctx->pixel_unpack;
    case GL_COPY_READ_BUFFER:
        return &ctx->copy_read;
    case GL_COPY_WRITE_BUFFER:
        return &ctx->copy_write;
    case GL_UNIFORM_BUFFER:
        return &ctx->uniform_buffer;
    case GL_SHADER_STORAGE_BUFFER:
        return gl_host_has(MIME_GPU_FEATURE_COMPUTE) ? &ctx->storage_buffer : nullptr;
    case GL_DISPATCH_INDIRECT_BUFFER:
        return gl_host_has(MIME_GPU_FEATURE_COMPUTE) ? &ctx->dispatch_indirect_buffer : nullptr;
    case GL_DRAW_INDIRECT_BUFFER:
        return &ctx->draw_indirect_buffer;
    case GL_PARAMETER_BUFFER:
        return &ctx->parameter_buffer;
    default:
        return nullptr;
    }
}

GlBuffer *gl_bound_buffer(GlContext *ctx, GLenum target)
{
    GLuint *b = binding_of(ctx, target);
    return b ? gl_buffer(ctx, *b) : nullptr;
}

GLAPI void APIENTRY glGenBuffers(GLsizei n, GLuint *buffers)
{
    GL_CONTEXT_OR_RETURN();
    for (GLsizei i = 0; i < n; i++)
        buffers[i] = gl_name_new(&ctx->shared->buffers);
}

GLAPI void APIENTRY glCreateBuffers(GLsizei n, GLuint *buffers)
{
    glGenBuffers(n, buffers);
}

static GlBuffer *make(GlContext *ctx, GLuint name)
{
    GlBuffer *b = gl_buffer(ctx, name);
    if (!b) {
        b = gl_alloc(sizeof(GlBuffer));
        if (!b)
            return nullptr;
        b->name = name;
        b->usage = GL_STATIC_DRAW;
        b->dirty_index_range = TRUE;
        gl_name_set(&ctx->shared->buffers, name, b);
    }
    return b;
}

GLAPI void APIENTRY glDeleteBuffers(GLsizei n, const GLuint *buffers)
{
    GL_CONTEXT_OR_RETURN();
    for (GLsizei i = 0; i < n; i++) {
        GLuint name = buffers[i];
        void *o = gl_name_get(&ctx->shared->buffers, name);
        if (!o)
            continue;
        if (o != GL_RESERVED) {
            GlBuffer *b = o;
            gl_heap_free(&b->mem, b->last_use);
            gl_free(b);
        }
        gl_name_set(&ctx->shared->buffers, name, nullptr);
        if (ctx->array_buffer == name)
            ctx->array_buffer = 0;
        if (ctx->vao->element_buffer == name)
            ctx->vao->element_buffer = 0;
        if (ctx->uniform_buffer == name)
            ctx->uniform_buffer = 0;
        if (ctx->storage_buffer == name)
            ctx->storage_buffer = 0;
        if (ctx->dispatch_indirect_buffer == name)
            ctx->dispatch_indirect_buffer = 0;
        if (ctx->draw_indirect_buffer == name)
            ctx->draw_indirect_buffer = 0;
        if (ctx->parameter_buffer == name)
            ctx->parameter_buffer = 0;
        for (int i = 0; i < MIME_GPU_MAX_UNIFORM_BLOCKS; i++)
            if (ctx->uniform_bindings[i].name == name)
                memset(&ctx->uniform_bindings[i], 0, sizeof(ctx->uniform_bindings[i]));
        for (int i = 0; i < MIME_GPU_MAX_STORAGE_BLOCKS; i++)
            if (ctx->storage_bindings[i].name == name)
                memset(&ctx->storage_bindings[i], 0, sizeof(ctx->storage_bindings[i]));
        for (int a = 0; a < MIME_GPU_MAX_ATTRIBS; a++)
            if (ctx->vao->attribs[a].buffer == name)
                ctx->vao->attribs[a].buffer = 0;
    }
    ctx->dirty |= DIRTY_VERTEX;
}

GLAPI GLboolean APIENTRY glIsBuffer(GLuint name)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    return gl_buffer(ctx, name) ? GL_TRUE : GL_FALSE;
}

GLAPI void APIENTRY glBindBuffer(GLenum target, GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    GLuint *b = binding_of(ctx, target);
    if (!b) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (name && !make(ctx, name)) {
        gl_error(ctx, GL_OUT_OF_MEMORY);
        return;
    }
    *b = name;
    if (target == GL_ELEMENT_ARRAY_BUFFER)
        ctx->dirty |= DIRTY_VERTEX;
}

/* the indexed binding of a target, with the generic binding it also sets, or NULL for a target that has none. */
static GlBufferBinding *indexed_binding(GlContext *ctx, GLenum target, GLuint index, GLuint **generic, GLuint *limit)
{
    switch (target) {
    case GL_UNIFORM_BUFFER:
        *generic = &ctx->uniform_buffer;
        *limit = MIME_GPU_MAX_UNIFORM_BLOCKS;
        return index < MIME_GPU_MAX_UNIFORM_BLOCKS ? &ctx->uniform_bindings[index] : nullptr;
    case GL_SHADER_STORAGE_BUFFER:
        if (!gl_host_has(MIME_GPU_FEATURE_COMPUTE))
            return nullptr;
        *generic = &ctx->storage_buffer;
        *limit = MIME_GPU_MAX_STORAGE_BLOCKS;
        return index < MIME_GPU_MAX_STORAGE_BLOCKS ? &ctx->storage_bindings[index] : nullptr;
    default:
        *limit = 0;
        return nullptr;
    }
}

static BOOL bind_indexed(GlContext *ctx, GLenum target, GLuint index, GLuint name, GLintptr offset, GLsizeiptr size,
                         BOOL whole)
{
    GLuint *generic = nullptr, limit = 0;
    GlBufferBinding *slot = indexed_binding(ctx, target, index, &generic, &limit);
    if (!slot) {
        gl_error(ctx, limit ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return FALSE;
    }
    if (!whole && name && (offset < 0 || size <= 0 || (offset & 255))) {
        gl_error(ctx, GL_INVALID_VALUE);
        return FALSE;
    }
    if (name && !make(ctx, name)) {
        gl_error(ctx, GL_OUT_OF_MEMORY);
        return FALSE;
    }
    *generic = name;
    if (whole)
        *slot = (GlBufferBinding){.name = name, .whole = TRUE};
    else
        *slot = (GlBufferBinding){name, name ? (uint64_t)offset : 0, name ? (uint64_t)size : 0, FALSE};
    return TRUE;
}

GLAPI void APIENTRY glBindBufferBase(GLenum target, GLuint index, GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    bind_indexed(ctx, target, index, name, 0, 0, TRUE);
}

GLAPI void APIENTRY glBindBufferRange(GLenum target, GLuint index, GLuint name, GLintptr offset, GLsizeiptr size)
{
    GL_CONTEXT_OR_RETURN();
    bind_indexed(ctx, target, index, name, offset, size, FALSE);
}

GLAPI void APIENTRY glBindBuffersBase(GLenum target, GLuint first, GLsizei count, const GLuint *buffers)
{
    GL_CONTEXT_OR_RETURN();
    GLuint *generic, limit;
    if (!indexed_binding(ctx, target, 0, &generic, &limit)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (count < 0 || first + (GLuint)count > limit) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    for (GLsizei i = 0; i < count; i++)
        if (!bind_indexed(ctx, target, first + (GLuint)i, buffers ? buffers[i] : 0, 0, 0, TRUE))
            return;
}

GLAPI void APIENTRY glBindBuffersRange(GLenum target, GLuint first, GLsizei count, const GLuint *buffers,
                                       const GLintptr *offsets, const GLsizeiptr *sizes)
{
    GL_CONTEXT_OR_RETURN();
    GLuint *generic, limit;
    if (!indexed_binding(ctx, target, 0, &generic, &limit)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (count < 0 || first + (GLuint)count > limit) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    for (GLsizei i = 0; i < count; i++) {
        GLuint name = buffers ? buffers[i] : 0;
        if (name && (!offsets || !sizes)) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
        if (!bind_indexed(ctx, target, first + (GLuint)i, name, name ? offsets[i] : 0, name ? sizes[i] : 0, FALSE))
            return;
    }
}

static BOOL in_use(GlBuffer *b)
{
    return b->last_use && !gl_fence_done(b->last_use);
}

/* a fence not emitted yet is emitted first, or the wait would be for a command nobody sent. */
static void buffer_wait(GlBuffer *b)
{
    if (!b->last_use || gl_fence_done(b->last_use))
        return;
    if (b->last_use > GlDev.fence)
        gl_fence();
    gl_wait(b->last_use);
}

/* waits for a buffer a shader may write, so that the cpu reads what the gpu wrote. */
BOOL gl_buffer_read_ready(GlBuffer *b)
{
    if (b->gpu_write && in_use(b))
        buffer_wait(b);
    return TRUE;
}

/* new storage of size, keeping the first keep bytes. */
static BOOL rename(GlBuffer *b, uint64_t size, uint64_t keep)
{
    GlMem mem;
    if (keep && b->gpu_write)
        gl_buffer_read_ready(b);
    if (!gl_heap_alloc(size ? size : 1, &mem))
        return FALSE;
    if (keep && b->mem.ptr)
        memcpy(mem.ptr, b->mem.ptr, keep < size ? keep : size);
    gl_heap_free(&b->mem, b->last_use);
    b->mem = mem;
    b->size = size;
    b->last_use = 0;
    b->dirty_index_range = TRUE;
    return TRUE;
}

GLAPI void APIENTRY glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    GL_CONTEXT_OR_RETURN();
    GlBuffer *b = gl_bound_buffer(ctx, target);
    if (!b || size < 0 || b->immutable) {
        gl_error(ctx, b && !b->immutable ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
        return;
    }
    b->usage = usage;
    if ((uint64_t)size != b->size || !b->mem.ptr || in_use(b)) {
        if (!rename(b, (uint64_t)size, 0)) {
            gl_error(ctx, GL_OUT_OF_MEMORY);
            return;
        }
    }
    if (data && size)
        memcpy(b->mem.ptr, data, (SIZE_T)size);
    b->dirty_index_range = TRUE;
    ctx->dirty |= DIRTY_VERTEX;
}

#define STORAGE_FLAGS (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | \
                       GL_DYNAMIC_STORAGE_BIT | GL_CLIENT_STORAGE_BIT)

GLAPI void APIENTRY glBufferStorage(GLenum target, GLsizeiptr size, const void *data, GLbitfield flags)
{
    GL_CONTEXT_OR_RETURN();
    GlBuffer *b = gl_bound_buffer(ctx, target);
    if (!b || b->immutable) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (size <= 0 || (flags & ~STORAGE_FLAGS) || ((flags & GL_MAP_PERSISTENT_BIT) && !(flags & (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT))) ||
        ((flags & GL_MAP_COHERENT_BIT) && !(flags & GL_MAP_PERSISTENT_BIT))) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!rename(b, (uint64_t)size, 0)) {
        gl_error(ctx, GL_OUT_OF_MEMORY);
        return;
    }
    if (data)
        memcpy(b->mem.ptr, data, (SIZE_T)size);
    b->usage = GL_STATIC_DRAW;
    b->immutable = TRUE;
    b->storage_flags = flags;
    b->dirty_index_range = TRUE;
    ctx->dirty |= DIRTY_VERTEX;
}

GLAPI void APIENTRY glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{
    GL_CONTEXT_OR_RETURN();
    GlBuffer *b = gl_bound_buffer(ctx, target);
    if (!b || offset < 0 || size < 0 || (uint64_t)(offset + size) > b->size) {
        gl_error(ctx, b ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
        return;
    }
    if (b->immutable && !(b->storage_flags & GL_DYNAMIC_STORAGE_BIT)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (!size || !data)
        return;
    /* a buffer that stays mapped keeps its memory. */
    if (in_use(b) && !(b->immutable && (b->storage_flags & GL_MAP_PERSISTENT_BIT)) && !rename(b, b->size, b->size)) {
        gl_error(ctx, GL_OUT_OF_MEMORY);
        return;
    }
    memcpy(b->mem.ptr + offset, data, (SIZE_T)size);
    b->dirty_index_range = TRUE;
    ctx->dirty |= DIRTY_VERTEX;
}

GLAPI void APIENTRY glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void *data)
{
    GL_CONTEXT_OR_RETURN();
    GlBuffer *b = gl_bound_buffer(ctx, target);
    if (!b || offset < 0 || size < 0 || (uint64_t)(offset + size) > b->size) {
        gl_error(ctx, b ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
        return;
    }
    /* the gpu writes buffers only through readbacks and shaders: they finish before the fence. */
    if (in_use(b))
        buffer_wait(b);
    memcpy(data, b->mem.ptr + offset, (SIZE_T)size);
}

GLAPI void *APIENTRY glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access)
{
    GL_CONTEXT_OR_RETURN(nullptr);
    GlBuffer *b = gl_bound_buffer(ctx, target);
    if (!b || b->mapped || offset < 0 || length < 0 || (uint64_t)(offset + length) > b->size) {
        gl_error(ctx, !b || b->mapped ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return nullptr;
    }
    if (b->immutable) {
        GLbitfield need = access & (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
        if ((need & ~b->storage_flags) || !(access & (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT))) {
            gl_error(ctx, GL_INVALID_OPERATION);
            return nullptr;
        }
    }
    BOOL persistent = (access & GL_MAP_PERSISTENT_BIT) != 0;
    if (!persistent && !(access & GL_MAP_UNSYNCHRONIZED_BIT) && in_use(b)) {
        uint64_t keep = (access & GL_MAP_INVALIDATE_BUFFER_BIT) ? 0 : b->size;
        if ((access & GL_MAP_READ_BIT) || b->immutable) {
            buffer_wait(b);
        } else if (!rename(b, b->size, keep)) {
            gl_error(ctx, GL_OUT_OF_MEMORY);
            return nullptr;
        }
    }
    b->mapped = TRUE;
    b->map_access = access;
    b->map_offset = (uint64_t)offset;
    b->map_length = (uint64_t)length;
    b->dirty_index_range = TRUE;
    return b->mem.ptr + offset;
}

GLAPI void *APIENTRY glMapBuffer(GLenum target, GLenum access)
{
    GL_CONTEXT_OR_RETURN(nullptr);
    GlBuffer *b = gl_bound_buffer(ctx, target);
    if (!b) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return nullptr;
    }
    GLbitfield bits = access == GL_READ_ONLY ? GL_MAP_READ_BIT
                      : access == GL_WRITE_ONLY ? GL_MAP_WRITE_BIT
                                                : GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
    return glMapBufferRange(target, 0, (GLsizeiptr)b->size, bits);
}

GLAPI GLboolean APIENTRY glUnmapBuffer(GLenum target)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    GlBuffer *b = gl_bound_buffer(ctx, target);
    if (!b || !b->mapped) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return GL_FALSE;
    }
    b->mapped = FALSE;
    ctx->dirty |= DIRTY_VERTEX;
    return GL_TRUE;
}

GLAPI void APIENTRY glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length)
{
}

GLAPI void APIENTRY glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GlBuffer *b = gl_bound_buffer(ctx, target);
    if (!b) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    switch (pname) {
    case GL_BUFFER_SIZE:
        *params = (GLint)b->size;
        break;
    case GL_BUFFER_USAGE:
        *params = (GLint)b->usage;
        break;
    case GL_BUFFER_MAPPED:
        *params = b->mapped;
        break;
    case GL_BUFFER_ACCESS:
        *params = GL_READ_WRITE;
        break;
    case GL_BUFFER_ACCESS_FLAGS:
        *params = (GLint)b->map_access;
        break;
    case GL_BUFFER_IMMUTABLE_STORAGE:
        *params = b->immutable;
        break;
    case GL_BUFFER_STORAGE_FLAGS:
        *params = (GLint)b->storage_flags;
        break;
    case GL_BUFFER_MAP_OFFSET:
        *params = (GLint)b->map_offset;
        break;
    case GL_BUFFER_MAP_LENGTH:
        *params = (GLint)b->map_length;
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        break;
    }
}

GLAPI void APIENTRY glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64 *params)
{
    GLint v = 0;
    glGetBufferParameteriv(target, pname, &v);
    *params = v;
}

GLAPI void APIENTRY glGetBufferPointerv(GLenum target, GLenum pname, void **params)
{
    GL_CONTEXT_OR_RETURN();
    GlBuffer *b = gl_bound_buffer(ctx, target);
    *params = b && b->mapped ? b->mem.ptr + b->map_offset : nullptr;
}

GLAPI void APIENTRY glCopyBufferSubData(GLenum read_target, GLenum write_target, GLintptr read_offset,
                                        GLintptr write_offset, GLsizeiptr size)
{
    GL_CONTEXT_OR_RETURN();
    GlBuffer *src = gl_bound_buffer(ctx, read_target), *dst = gl_bound_buffer(ctx, write_target);
    if (!src || !dst || read_offset < 0 || write_offset < 0 || size < 0 ||
        (uint64_t)(read_offset + size) > src->size || (uint64_t)(write_offset + size) > dst->size) {
        gl_error(ctx, !src || !dst ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }
    if (in_use(dst) && !rename(dst, dst->size, dst->size)) {
        gl_error(ctx, GL_OUT_OF_MEMORY);
        return;
    }
    memmove(dst->mem.ptr + write_offset, src->mem.ptr + read_offset, (SIZE_T)size);
    dst->dirty_index_range = TRUE;
    ctx->dirty |= DIRTY_VERTEX;
}

/* ---------------------------------------------------------------- clearing and invalidating */

typedef struct ClearFormat {
    GLenum internal;
    uint8_t components, bytes; /* of each component. */
} ClearFormat;

static const ClearFormat g_clear_formats[] = {
    {GL_R8, 1, 1},      {GL_R8UI, 1, 1},     {GL_R8I, 1, 1},      {GL_R16, 1, 2},      {GL_R16UI, 1, 2},
    {GL_R16I, 1, 2},    {GL_R32F, 1, 4},     {GL_R32UI, 1, 4},    {GL_R32I, 1, 4},     {GL_RG8, 2, 1},
    {GL_RG8UI, 2, 1},   {GL_RG8I, 2, 1},     {GL_RG16, 2, 2},     {GL_RG16UI, 2, 2},   {GL_RG16I, 2, 2},
    {GL_RG32F, 2, 4},   {GL_RG32UI, 2, 4},   {GL_RG32I, 2, 4},    {GL_RGB32F, 3, 4},   {GL_RGB32UI, 3, 4},
    {GL_RGB32I, 3, 4},  {GL_RGBA8, 4, 1},    {GL_RGBA8UI, 4, 1},  {GL_RGBA8I, 4, 1},   {GL_RGBA16, 4, 2},
    {GL_RGBA16UI, 4, 2}, {GL_RGBA16I, 4, 2}, {GL_RGBA32F, 4, 4}, {GL_RGBA32UI, 4, 4}, {GL_RGBA32I, 4, 4},
};

static uint32_t type_bytes_of(GLenum type)
{
    switch (type) {
    case GL_UNSIGNED_BYTE:
    case GL_BYTE:
        return 1;
    case GL_UNSIGNED_SHORT:
    case GL_SHORT:
        return 2;
    case GL_UNSIGNED_INT:
    case GL_INT:
    case GL_FLOAT:
        return 4;
    default:
        return 0;
    }
}

static uint32_t format_components(GLenum format)
{
    switch (format) {
    case GL_RED:
    case GL_RED_INTEGER:
        return 1;
    case GL_RG:
    case GL_RG_INTEGER:
        return 2;
    case GL_RGB:
    case GL_RGB_INTEGER:
        return 3;
    case GL_RGBA:
    case GL_RGBA_INTEGER:
        return 4;
    default:
        return 0;
    }
}

/*
 * the bytes of one element of the internal format from the value the call gives, whose components must have the size the
 * format stores them in. a value of fewer components than the format has fills the rest with zero, alpha with one.
 */
static BOOL clear_element(GLenum internal, GLenum format, GLenum type, const void *data, uint8_t *out, uint32_t *bytes)
{
    const ClearFormat *f = nullptr;
    for (size_t i = 0; i < sizeof(g_clear_formats) / sizeof(g_clear_formats[0]); i++)
        if (g_clear_formats[i].internal == internal)
            f = &g_clear_formats[i];
    if (!f)
        return FALSE;
    *bytes = (uint32_t)f->components * f->bytes;
    memset(out, 0, *bytes);
    if (!data)
        return TRUE;
    uint32_t have = format_components(format), size = type_bytes_of(type);
    if (!have || size != f->bytes)
        return FALSE;
    for (uint32_t c = 0; c < f->components; c++) {
        if (c < have)
            memcpy(out + c * f->bytes, (const uint8_t *)data + c * size, size);
        else if (c == 3 && (type == GL_FLOAT || f->bytes == 4)) {
            GLfloat one = 1.0f;
            GLuint integer_one = 1;
            if (type == GL_FLOAT)
                memcpy(out + c * 4, &one, 4);
            else
                memcpy(out + c * 4, &integer_one, 4);
        }
    }
    return TRUE;
}

GLAPI void APIENTRY glClearBufferSubData(GLenum target, GLenum internal, GLintptr offset, GLsizeiptr size, GLenum format,
                                         GLenum type, const void *data)
{
    GL_CONTEXT_OR_RETURN();
    GlBuffer *b = gl_bound_buffer(ctx, target);
    if (!b) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    uint8_t element[16];
    uint32_t bytes;
    if (!clear_element(internal, format, type, data, element, &bytes)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (offset < 0 || size < 0 || (uint64_t)(offset + size) > b->size || (offset % bytes) || (size % bytes)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (b->mapped && !(b->map_access & GL_MAP_PERSISTENT_BIT)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (!size)
        return;
    /* the gpu may be reading or writing it: the fill waits, there is nothing to rename. */
    if (b->last_use && !gl_fence_done(b->last_use))
        buffer_wait(b);
    for (GLsizeiptr at = 0; at < size; at += bytes)
        memcpy(b->mem.ptr + offset + at, element, bytes);
    b->dirty_index_range = TRUE;
    ctx->dirty |= DIRTY_VERTEX;
}

GLAPI void APIENTRY glClearBufferData(GLenum target, GLenum internal, GLenum format, GLenum type, const void *data)
{
    GL_CONTEXT_OR_RETURN();
    GlBuffer *b = gl_bound_buffer(ctx, target);
    if (!b) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    glClearBufferSubData(target, internal, 0, (GLsizeiptr)b->size, format, type, data);
}

GLAPI void APIENTRY glInvalidateBufferData(GLuint buffer)
{
    GL_CONTEXT_OR_RETURN();
    if (!gl_buffer(ctx, buffer))
        gl_error(ctx, GL_INVALID_VALUE);
}

GLAPI void APIENTRY glInvalidateBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr length)
{
    GL_CONTEXT_OR_RETURN();
    GlBuffer *b = gl_buffer(ctx, buffer);
    if (!b || offset < 0 || length < 0 || (uint64_t)(offset + length) > b->size)
        gl_error(ctx, GL_INVALID_VALUE);
}
