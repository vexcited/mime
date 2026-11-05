/*
 * compute: shader storage buffers, images, dispatches and memory barriers, and the program interface queries.
 *
 * the host runs a compute program on its own pipeline and shares the descriptor set layout of draws: a storage block is
 * a slot by its index in the program, like uniform blocks, an image is a slot by its place among the images of the
 * program. the bindings of the context are resolved here and sent before each draw or dispatch that uses them.
 */
#include "opengl32.h"

GLint APIENTRY glGetUniformLocation(GLuint program, const GLchar *name);
GLint APIENTRY glGetAttribLocation(GLuint program, const GLchar *name);
void APIENTRY glMemoryBarrier(GLbitfield barriers);

BOOL gl_is_image_type(GLenum type)
{
    return type >= GL_IMAGE_1D && type <= GL_UNSIGNED_INT_IMAGE_2D_MULTISAMPLE_ARRAY;
}

/* ---------------------------------------------------------------- bindings */

GLAPI void APIENTRY glShaderStorageBlockBinding(GLuint program, GLuint index, GLuint binding)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_program(ctx, program);
    if (!p || !gl_program_ready(p)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (index >= p->storage_count || binding >= MIME_GPU_MAX_STORAGE_BLOCKS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    p->storage[index].binding = binding;
}

/* sends the buffer of each storage block of the program. the device lock must be held. */
BOOL gl_emit_storage(GlContext *ctx, GlProgram *p)
{
    for (uint32_t i = 0; i < p->storage_count; i++) {
        const GlBufferBinding *binding = &ctx->storage_bindings[p->storage[i].binding];
        GlBuffer *buffer = gl_buffer(ctx, binding->name);
        MimeGpuSetStorageBuffer *c = gl_emit(MIME_GPU_SET_STORAGE_BUFFER, sizeof(MimeGpuSetStorageBuffer));
        c->slot = i;
        if (!buffer || !buffer->mem.ptr || binding->offset >= buffer->size)
            continue;
        if (buffer->mapped && !(buffer->map_access & GL_MAP_PERSISTENT_BIT))
            continue;
        uint64_t size = binding->whole ? buffer->size : binding->size;
        if (size > buffer->size - binding->offset)
            size = buffer->size - binding->offset;
        c->mem = gl_mem(&buffer->mem);
        c->mem.offset += binding->offset;
        c->size = size;
        buffer->gpu_write = TRUE;
        buffer->last_use = gl_batch_fence();
    }
    return TRUE;
}

/* sends the texture each image uniform of the program names through its image unit. */
void gl_emit_images(GlContext *ctx, GlProgram *p)
{
    for (uint32_t i = 0; i < p->uniform_count; i++) {
        const GlUniform *u = &p->uniforms[i];
        if (!u->units || !gl_is_image_type(u->type) || u->texture[0] < 0)
            continue;
        for (GLint e = 0; e < u->size; e++) {
            GLint unit = u->units[e];
            uint32_t slot = (uint32_t)u->texture[0] + (uint32_t)e;
            if (slot >= MIME_GPU_MAX_IMAGES)
                continue;
            MimeGpuSetImage *c = gl_emit(MIME_GPU_SET_IMAGE, sizeof(MimeGpuSetImage));
            c->slot = slot;
            if (unit < 0 || unit >= MIME_GPU_MAX_IMAGES)
                continue;
            GlTexture *t = gl_texture(ctx, ctx->image_units[unit].texture);
            if (!t || !gl_texture_ready(ctx, t))
                continue;
            c->id = t->id;
            c->level = (uint32_t)ctx->image_units[unit].level;
            c->layer = (uint32_t)ctx->image_units[unit].layer;
            c->layered = ctx->image_units[unit].layered;
        }
    }
}

static BOOL image_access(GLenum access)
{
    return access == GL_READ_ONLY || access == GL_WRITE_ONLY || access == GL_READ_WRITE;
}

GLAPI void APIENTRY glBindImageTexture(GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer,
                                       GLenum access, GLenum format)
{
    GL_CONTEXT_OR_RETURN();
    if (unit >= MIME_GPU_MAX_IMAGES || level < 0 || layer < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (texture && !gl_texture(ctx, texture)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!image_access(access)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    ctx->image_units[unit].texture = texture;
    ctx->image_units[unit].level = level;
    ctx->image_units[unit].layered = layered;
    ctx->image_units[unit].layer = layer;
    ctx->image_units[unit].access = access;
    ctx->image_units[unit].format = format;
}

GLAPI void APIENTRY glBindImageTextures(GLuint first, GLsizei count, const GLuint *textures)
{
    GL_CONTEXT_OR_RETURN();
    if (count < 0 || first + (GLuint)count > MIME_GPU_MAX_IMAGES) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    for (GLsizei i = 0; textures && i < count; i++)
        if (textures[i] && !gl_texture(ctx, textures[i])) {
            gl_error(ctx, GL_INVALID_OPERATION);
            return;
        }
    for (GLsizei i = 0; i < count; i++) {
        GLuint name = textures ? textures[i] : 0;
        GlTexture *t = gl_texture(ctx, name);
        GLuint unit = first + (GLuint)i;
        ctx->image_units[unit].texture = name;
        ctx->image_units[unit].level = 0;
        ctx->image_units[unit].layered = GL_TRUE;
        ctx->image_units[unit].layer = 0;
        ctx->image_units[unit].access = GL_READ_WRITE;
        ctx->image_units[unit].format = t ? t->internal_format : GL_RGBA8;
    }
}

/* ---------------------------------------------------------------- dispatch and barriers */

#define MAX_GROUPS 65535u
#define MAX_INVOCATIONS 1024u

static void dispatch(GlContext *ctx, GLuint x, GLuint y, GLuint z)
{
    GlProgram *p = ctx->current_program;
    if (!p || !p->compute || !gl_program_ready(p)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (x > MAX_GROUPS || y > MAX_GROUPS || z > MAX_GROUPS) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!x || !y || !z)
        return;
    gl_device_lock();
    gl_flush_clears(ctx);
    gl_emit_compute_state(ctx, p);
    gl_emit_storage(ctx, p);
    gl_emit_images(ctx, p);
    MimeGpuDispatch *d = gl_emit(MIME_GPU_DISPATCH, sizeof(MimeGpuDispatch));
    d->program = p->id;
    d->x = x;
    d->y = y;
    d->z = z;
    gl_device_unlock();
}

GLAPI void APIENTRY glDispatchCompute(GLuint x, GLuint y, GLuint z)
{
    GL_CONTEXT_OR_RETURN();
    dispatch(ctx, x, y, z);
}

GLAPI void APIENTRY glDispatchComputeIndirect(GLintptr indirect)
{
    GL_CONTEXT_OR_RETURN();
    GlBuffer *b = gl_buffer(ctx, ctx->dispatch_indirect_buffer);
    if (!b || indirect < 0 || (indirect & 3) || (uint64_t)indirect + 12 > b->size) {
        gl_error(ctx, !b ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }
    /* the groups may come from a shader: they are read once the gpu is done with the buffer. */
    gl_buffer_read_ready(b);
    GLuint groups[3];
    memcpy(groups, b->mem.ptr + indirect, sizeof(groups));
    dispatch(ctx, groups[0], groups[1], groups[2]);
}

GLAPI void APIENTRY glMemoryBarrier(GLbitfield barriers)
{
    GL_CONTEXT_OR_RETURN();
    if (barriers != GL_ALL_BARRIER_BITS && (barriers & ~(GLbitfield)(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT |
        GL_ELEMENT_ARRAY_BARRIER_BIT | GL_UNIFORM_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
        GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_COMMAND_BARRIER_BIT | GL_PIXEL_BUFFER_BARRIER_BIT |
        GL_TEXTURE_UPDATE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT |
        GL_TRANSFORM_FEEDBACK_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT |
        GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT | GL_QUERY_BUFFER_BARRIER_BIT))) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    gl_device_lock();
    gl_flush_clears(ctx);
    gl_emit(MIME_GPU_MEMORY_BARRIER, sizeof(MimeGpuCmd));
    gl_device_unlock();
}

GLAPI void APIENTRY glMemoryBarrierByRegion(GLbitfield barriers)
{
    GL_CONTEXT_OR_RETURN();
    if (barriers != GL_ALL_BARRIER_BITS && (barriers & ~(GLbitfield)(GL_ATOMIC_COUNTER_BARRIER_BIT |
        GL_FRAMEBUFFER_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT |
        GL_TEXTURE_FETCH_BARRIER_BIT | GL_UNIFORM_BARRIER_BIT))) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
}

/* ---------------------------------------------------------------- limits */

int gl_compute_get(GlContext *ctx, GLenum pname, GLdouble *v)
{
    if (!gl_host_has(MIME_GPU_FEATURE_COMPUTE))
        return 0;
    switch (pname) {
    case GL_SHADER_STORAGE_BUFFER_BINDING:
        v[0] = ctx->storage_buffer;
        return 1;
    case GL_DISPATCH_INDIRECT_BUFFER_BINDING:
        v[0] = ctx->dispatch_indirect_buffer;
        return 1;
    case GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS:
    case GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS:
    case GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS:
    case GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS:
    case GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS:
        v[0] = MIME_GPU_MAX_STORAGE_BLOCKS;
        return 1;
    case GL_MAX_SHADER_STORAGE_BLOCK_SIZE:
        v[0] = 1 << 27;
        return 1;
    case GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT:
        v[0] = 256;
        return 1;
    case GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS:
        v[0] = MAX_INVOCATIONS;
        return 1;
    case GL_MAX_COMPUTE_SHARED_MEMORY_SIZE:
        v[0] = 32768;
        return 1;
    case GL_MAX_COMPUTE_UNIFORM_COMPONENTS:
        v[0] = 1024;
        return 1;
    case GL_MAX_COMPUTE_UNIFORM_BLOCKS:
        v[0] = MIME_GPU_MAX_UNIFORM_BLOCKS;
        return 1;
    case GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS:
        v[0] = 16;
        return 1;
    case GL_MAX_IMAGE_UNITS:
    case GL_MAX_COMBINED_IMAGE_UNIFORMS:
    case GL_MAX_COMPUTE_IMAGE_UNIFORMS:
    case GL_MAX_VERTEX_IMAGE_UNIFORMS:
    case GL_MAX_FRAGMENT_IMAGE_UNIFORMS:
        v[0] = MIME_GPU_MAX_IMAGES;
        return 1;
    case GL_MAX_IMAGE_SAMPLES:
        v[0] = 1;
        return 1;
    case GL_MAX_COMBINED_SHADER_OUTPUT_RESOURCES:
        v[0] = MIME_GPU_MAX_STORAGE_BLOCKS + MIME_GPU_MAX_IMAGES;
        return 1;
    case GL_MAX_COMPUTE_WORK_GROUP_COUNT:
        v[0] = v[1] = v[2] = MAX_GROUPS;
        return 3;
    case GL_MAX_COMPUTE_WORK_GROUP_SIZE:
        v[0] = 1024;
        v[1] = 1024;
        v[2] = 64;
        return 3;
    default:
        return 0;
    }
}

BOOL gl_compute_get_indexed(GlContext *ctx, GLenum target, GLuint index, GLint *data)
{
    if (!gl_host_has(MIME_GPU_FEATURE_COMPUTE))
        return FALSE;
    switch (target) {
    case GL_MAX_COMPUTE_WORK_GROUP_COUNT:
    case GL_MAX_COMPUTE_WORK_GROUP_SIZE:
        if (index > 2) {
            gl_error(ctx, GL_INVALID_VALUE);
            return TRUE;
        }
        *data = target == GL_MAX_COMPUTE_WORK_GROUP_COUNT ? (GLint)MAX_GROUPS : index == 2 ? 64 : 1024;
        return TRUE;
    case GL_SHADER_STORAGE_BUFFER_BINDING:
    case GL_SHADER_STORAGE_BUFFER_START:
    case GL_SHADER_STORAGE_BUFFER_SIZE: {
        if (index >= MIME_GPU_MAX_STORAGE_BLOCKS) {
            gl_error(ctx, GL_INVALID_VALUE);
            return TRUE;
        }
        const GlBufferBinding *b = &ctx->storage_bindings[index];
        GlBuffer *buffer = gl_buffer(ctx, b->name);
        *data = target == GL_SHADER_STORAGE_BUFFER_BINDING ? (GLint)b->name
                : target == GL_SHADER_STORAGE_BUFFER_START ? (GLint)b->offset
                                                           : (GLint)(b->whole && buffer ? buffer->size : b->size);
        return TRUE;
    }
    case GL_IMAGE_BINDING_NAME:
    case GL_IMAGE_BINDING_LEVEL:
    case GL_IMAGE_BINDING_LAYERED:
    case GL_IMAGE_BINDING_LAYER:
    case GL_IMAGE_BINDING_ACCESS:
    case GL_IMAGE_BINDING_FORMAT: {
        if (index >= MIME_GPU_MAX_IMAGES) {
            gl_error(ctx, GL_INVALID_VALUE);
            return TRUE;
        }
        *data = target == GL_IMAGE_BINDING_NAME      ? (GLint)ctx->image_units[index].texture
                : target == GL_IMAGE_BINDING_LEVEL   ? ctx->image_units[index].level
                : target == GL_IMAGE_BINDING_LAYERED ? ctx->image_units[index].layered
                : target == GL_IMAGE_BINDING_LAYER   ? ctx->image_units[index].layer
                : target == GL_IMAGE_BINDING_ACCESS  ? (GLint)(ctx->image_units[index].access ? ctx->image_units[index].access
                                                                                              : GL_READ_ONLY)
                                                     : (GLint)ctx->image_units[index].format;
        return TRUE;
    }
    default:
        return FALSE;
    }
}

/* ---------------------------------------------------------------- program interface queries */

static GlProgram *interface_program(GlContext *ctx, GLuint program)
{
    GlProgram *p = gl_program(ctx, program);
    if (!p) {
        gl_error(ctx, GL_INVALID_VALUE);
        return nullptr;
    }
    if (!gl_program_ready(p)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return nullptr;
    }
    return p;
}

static BOOL known_interface(GLenum iface)
{
    switch (iface) {
    case GL_UNIFORM:
    case GL_UNIFORM_BLOCK:
    case GL_SHADER_STORAGE_BLOCK:
    case GL_PROGRAM_INPUT:
    case GL_PROGRAM_OUTPUT:
    case GL_BUFFER_VARIABLE:
    case GL_ATOMIC_COUNTER_BUFFER:
        return TRUE;
    default:
        return FALSE;
    }
}

/* the number of resources and the name of one. the program outputs, buffer variables and atomic counters have none. */
static uint32_t resource_count(GlProgram *p, GLenum iface)
{
    switch (iface) {
    case GL_UNIFORM:
        return p->uniform_count;
    case GL_UNIFORM_BLOCK:
        return p->block_count;
    case GL_SHADER_STORAGE_BLOCK:
        return p->storage_count;
    case GL_PROGRAM_INPUT:
        return p->compute ? 0 : p->attrib_count;
    default:
        return 0;
    }
}

static const char *resource_name(GlProgram *p, GLenum iface, uint32_t index)
{
    switch (iface) {
    case GL_UNIFORM:
        return p->uniforms[index].name;
    case GL_UNIFORM_BLOCK:
        return p->blocks[index].name;
    case GL_SHADER_STORAGE_BLOCK:
        return p->storage[index].name;
    default:
        return p->attribs[index].name;
    }
}

GLAPI void APIENTRY glGetProgramInterfaceiv(GLuint program, GLenum iface, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = interface_program(ctx, program);
    if (!p)
        return;
    if (!known_interface(iface)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    uint32_t n = resource_count(p, iface);
    GLint value = 0;
    switch (pname) {
    case GL_ACTIVE_RESOURCES:
        value = (GLint)n;
        break;
    case GL_MAX_NAME_LENGTH:
        if (iface == GL_ATOMIC_COUNTER_BUFFER) {
            gl_error(ctx, GL_INVALID_OPERATION);
            return;
        }
        for (uint32_t i = 0; i < n; i++)
            if ((GLint)strlen(resource_name(p, iface, i)) + 1 > value)
                value = (GLint)strlen(resource_name(p, iface, i)) + 1;
        break;
    case GL_MAX_NUM_ACTIVE_VARIABLES:
        if (iface != GL_UNIFORM_BLOCK && iface != GL_SHADER_STORAGE_BLOCK && iface != GL_ATOMIC_COUNTER_BUFFER) {
            gl_error(ctx, GL_INVALID_OPERATION);
            return;
        }
        if (iface == GL_SHADER_STORAGE_BLOCK)
            for (uint32_t i = 0; i < n; i++)
                if ((GLint)p->storage[i].variables > value)
                    value = (GLint)p->storage[i].variables;
        break;
    default:
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (params)
        *params = value;
}

/* a name matches a resource when it is the whole name, or the name of an array without its [0]. */
static BOOL name_matches(const char *resource, const char *name)
{
    SIZE_T n = strlen(name);
    if (!strncmp(resource, name, n) && !resource[n])
        return TRUE;
    return !strncmp(resource, name, n) && !strcmp(resource + n, "[0]");
}

GLAPI GLuint APIENTRY glGetProgramResourceIndex(GLuint program, GLenum iface, const GLchar *name)
{
    GL_CONTEXT_OR_RETURN(GL_INVALID_INDEX);
    GlProgram *p = interface_program(ctx, program);
    if (!p)
        return GL_INVALID_INDEX;
    if (!known_interface(iface) || iface == GL_ATOMIC_COUNTER_BUFFER) {
        gl_error(ctx, GL_INVALID_ENUM);
        return GL_INVALID_INDEX;
    }
    uint32_t n = resource_count(p, iface);
    for (uint32_t i = 0; name && i < n; i++)
        if (name_matches(resource_name(p, iface, i), name))
            return i;
    return GL_INVALID_INDEX;
}

GLAPI void APIENTRY glGetProgramResourceName(GLuint program, GLenum iface, GLuint index, GLsizei size, GLsizei *length,
                                              GLchar *name)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = interface_program(ctx, program);
    if (!p)
        return;
    if (!known_interface(iface) || iface == GL_ATOMIC_COUNTER_BUFFER) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (index >= resource_count(p, iface) || size < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    const char *text = resource_name(p, iface, index);
    SIZE_T count = size > 0 ? strlen(text) : 0;
    if (size > 0 && count >= (SIZE_T)size)
        count = (SIZE_T)size - 1;
    if (size > 0 && name) {
        memcpy(name, text, count);
        name[count] = 0;
    }
    if (length)
        *length = (GLsizei)count;
}

static BOOL resource_property(GlProgram *p, GLenum iface, uint32_t index, GLenum prop, GLint *out)
{
    GLint stage_mask = p->compute ? 4 : 3;
    const GlUniform *u = iface == GL_UNIFORM ? &p->uniforms[index] : nullptr;
    switch (prop) {
    case GL_NAME_LENGTH:
        *out = (GLint)strlen(resource_name(p, iface, index)) + 1;
        return iface != GL_ATOMIC_COUNTER_BUFFER;
    case GL_TYPE:
        *out = u ? (GLint)u->type : iface == GL_PROGRAM_INPUT ? (GLint)p->attribs[index].type : 0;
        return u || iface == GL_PROGRAM_INPUT;
    case GL_ARRAY_SIZE:
        *out = u ? u->size : iface == GL_PROGRAM_INPUT ? p->attribs[index].size : 0;
        return u || iface == GL_PROGRAM_INPUT;
    case GL_LOCATION:
        *out = u ? (GLint)(index * 4096) : iface == GL_PROGRAM_INPUT ? p->attribs[index].location : -1;
        return u || iface == GL_PROGRAM_INPUT;
    case GL_OFFSET:
        *out = u && !u->units ? (GLint)u->offset : -1;
        return u != nullptr;
    case GL_BLOCK_INDEX:
    case GL_ATOMIC_COUNTER_BUFFER_INDEX:
        *out = -1;
        return u != nullptr;
    case GL_ARRAY_STRIDE:
        *out = u && u->size > 1 ? (GLint)u->array_stride : u ? 0 : -1;
        return u != nullptr;
    case GL_MATRIX_STRIDE:
        *out = u ? (GLint)u->matrix_stride : -1;
        return u != nullptr;
    case GL_IS_ROW_MAJOR:
        *out = 0;
        return u != nullptr;
    case GL_BUFFER_BINDING:
        if (iface == GL_UNIFORM_BLOCK)
            *out = (GLint)p->blocks[index].binding;
        else if (iface == GL_SHADER_STORAGE_BLOCK)
            *out = (GLint)p->storage[index].binding;
        else
            return FALSE;
        return TRUE;
    case GL_BUFFER_DATA_SIZE:
        if (iface == GL_UNIFORM_BLOCK)
            *out = (GLint)p->blocks[index].bytes;
        else if (iface == GL_SHADER_STORAGE_BLOCK)
            *out = (GLint)p->storage[index].bytes;
        else
            return FALSE;
        return TRUE;
    case GL_NUM_ACTIVE_VARIABLES:
        if (iface == GL_SHADER_STORAGE_BLOCK)
            *out = (GLint)p->storage[index].variables;
        else if (iface == GL_UNIFORM_BLOCK)
            *out = 0;
        else
            return FALSE;
        return TRUE;
    case GL_REFERENCED_BY_VERTEX_SHADER:
    case GL_REFERENCED_BY_FRAGMENT_SHADER:
    case GL_REFERENCED_BY_COMPUTE_SHADER: {
        GLint bit = prop == GL_REFERENCED_BY_VERTEX_SHADER ? 1 : prop == GL_REFERENCED_BY_FRAGMENT_SHADER ? 2 : 4;
        GLint mask = iface == GL_UNIFORM_BLOCK ? (GLint)p->blocks[index].stages
                     : iface == GL_SHADER_STORAGE_BLOCK ? (GLint)p->storage[index].stages
                     : iface == GL_PROGRAM_INPUT        ? 1
                                                        : stage_mask;
        if (p->compute && (iface == GL_UNIFORM_BLOCK || iface == GL_SHADER_STORAGE_BLOCK))
            mask = 4;
        *out = (mask & bit) != 0;
        return TRUE;
    }
    default:
        return FALSE;
    }
}

GLAPI void APIENTRY glGetProgramResourceiv(GLuint program, GLenum iface, GLuint index, GLsizei prop_count,
                                            const GLenum *props, GLsizei size, GLsizei *length, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = interface_program(ctx, program);
    if (!p)
        return;
    if (!known_interface(iface)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (index >= resource_count(p, iface) || prop_count <= 0 || size < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    GLsizei written = 0;
    for (GLsizei i = 0; i < prop_count && written < size; i++) {
        GLint value;
        if (!resource_property(p, iface, index, props[i], &value)) {
            gl_error(ctx, GL_INVALID_ENUM);
            return;
        }
        params[written++] = value;
    }
    if (length)
        *length = written;
}

GLAPI GLint APIENTRY glGetProgramResourceLocation(GLuint program, GLenum iface, const GLchar *name)
{
    GL_CONTEXT_OR_RETURN(-1);
    GlProgram *p = interface_program(ctx, program);
    if (!p)
        return -1;
    if (iface != GL_UNIFORM && iface != GL_PROGRAM_INPUT && iface != GL_PROGRAM_OUTPUT) {
        gl_error(ctx, GL_INVALID_ENUM);
        return -1;
    }
    if (iface == GL_UNIFORM)
        return glGetUniformLocation(program, name);
    if (iface == GL_PROGRAM_INPUT)
        return glGetAttribLocation(program, name);
    return -1;
}

GLAPI GLint APIENTRY glGetProgramResourceLocationIndex(GLuint program, GLenum iface, const GLchar *name)
{
    GL_CONTEXT_OR_RETURN(-1);
    GlProgram *p = interface_program(ctx, program);
    if (!p)
        return -1;
    if (iface != GL_PROGRAM_OUTPUT) {
        gl_error(ctx, GL_INVALID_ENUM);
        return -1;
    }
    return -1;
}
