/*
 * glPushAttrib and glPopAttrib, glPushClientAttrib and glPopClientAttrib. a push copies the state of the groups the
 * mask names, and a pop writes it back, group by group, so what the groups share is restored in the order of the
 * groups. the stacks are 16 deep, as Windows has them.
 */
#include "opengl32.h"

#define ATTRIB_DEPTH 16

typedef struct AttribEntry {
    GLbitfield mask;
    struct {
        GLfloat color[4], normal[4], texcoord[4];
        GLfloat extra[FF_UNITS][4];
        GLboolean edge_flag;
    } current;
    GLfloat point_size;
    struct {
        GLfloat width;
    } line;
    struct {
        BOOL cull_face, offset_fill;
        GLenum cull_mode, front_face, mode;
        GLfloat factor, units;
    } polygon;
    struct {
        BOOL lighting, color_material, local_viewer, two_side;
        GLenum shade_model, material_face, material_mode, color_control;
        GLfloat model_ambient[4];
        GlMaterial material[2];
        GlLight lights[FF_LIGHTS];
    } lighting;
    struct {
        BOOL enabled;
        GLenum mode;
        GLfloat density, start, end, color[4];
    } fog;
    struct {
        BOOL test;
        GLenum func;
        GLboolean mask;
        GLdouble clear;
    } depth;
    struct {
        BOOL test;
        GLenum func[2], fail[2], zfail[2], zpass[2];
        GLint ref[2];
        GLuint value_mask[2], write_mask[2];
        GLint clear;
    } stencil;
    struct {
        GLint box[4];
        GLdouble range[2];
    } viewport;
    struct {
        GLenum matrix_mode;
        BOOL normalize, rescale_normal;
    } transform;
    struct {
        BOOL blend, depth, stencil, scissor, cull, offset_fill, dither, multisample, alpha_to_coverage;
        BOOL lighting, alpha_test, fog, normalize, rescale_normal, color_material;
        BOOL texture_1d[FF_UNITS], texture_2d[FF_UNITS], texture_3d[FF_UNITS], lights[FF_LIGHTS];
        BOOL stored[FF_STORED_CAPS];
    } enable;
    struct {
        BOOL alpha_test, blend, dither;
        GLenum alpha_func;
        GLfloat alpha_ref;
        GlBlend blend_state;
        GLfloat blend_color[4];
        GLboolean mask[4];
        GLfloat clear[4];
        GLenum draw_buffer;
    } color_buffer;
    struct {
        GLuint units[FF_UNITS][8];
        GLenum env_mode[FF_UNITS];
        GLfloat env_color[FF_UNITS][4];
        GLuint active;
    } texture;
    struct {
        BOOL test;
        GLint box[4];
    } scissor;
} AttribEntry;

typedef struct ClientEntry {
    GLbitfield mask;
    GlVertexArray array;
    GLuint array_buffer;
    GLuint client_unit;
    BOOL unused[4];
    GLint pack[3], unpack[5], pack_alignment, unpack_alignment;
} ClientEntry;

static void save(GlContext *ctx, AttribEntry *e, GLbitfield mask)
{
    GlFixed *f = &ctx->ff;
    e->mask = mask;
    if (mask & GL_CURRENT_BIT) {
        memcpy(e->current.color, f->current[0], 16);
        memcpy(e->current.normal, f->current[1], 16);
        memcpy(e->current.texcoord, f->current[2], 16);
        memcpy(e->current.extra, f->texcoord, sizeof(f->texcoord));
        e->current.edge_flag = f->edge_flag;
    }
    e->point_size = ctx->point_size;
    e->line.width = ctx->line_width;
    e->polygon.cull_face = ctx->cull_face;
    e->polygon.offset_fill = ctx->polygon_offset_fill;
    e->polygon.cull_mode = ctx->cull_mode;
    e->polygon.front_face = ctx->front_face;
    e->polygon.mode = ctx->polygon_mode;
    e->polygon.factor = ctx->offset_factor;
    e->polygon.units = ctx->offset_units;
    e->lighting.lighting = f->lighting;
    e->lighting.color_material = f->color_material;
    e->lighting.local_viewer = f->local_viewer;
    e->lighting.two_side = f->two_side;
    e->lighting.shade_model = f->shade_model;
    e->lighting.material_face = f->color_material_face;
    e->lighting.material_mode = f->color_material_mode;
    e->lighting.color_control = f->color_control;
    memcpy(e->lighting.model_ambient, f->model_ambient, 16);
    memcpy(e->lighting.material, f->material, sizeof(f->material));
    memcpy(e->lighting.lights, f->lights, sizeof(f->lights));
    e->fog.enabled = f->fog;
    e->fog.mode = f->fog_mode;
    e->fog.density = f->fog_density;
    e->fog.start = f->fog_start;
    e->fog.end = f->fog_end;
    memcpy(e->fog.color, f->fog_color, 16);
    e->depth.test = ctx->depth_test;
    e->depth.func = ctx->depth_func;
    e->depth.mask = ctx->depth_mask;
    e->depth.clear = ctx->clear_depth;
    e->stencil.test = ctx->stencil_test;
    memcpy(e->stencil.func, ctx->stencil_func, sizeof(e->stencil.func));
    memcpy(e->stencil.fail, ctx->stencil_fail, sizeof(e->stencil.fail));
    memcpy(e->stencil.zfail, ctx->stencil_zfail, sizeof(e->stencil.zfail));
    memcpy(e->stencil.zpass, ctx->stencil_zpass, sizeof(e->stencil.zpass));
    memcpy(e->stencil.ref, ctx->stencil_ref, sizeof(e->stencil.ref));
    memcpy(e->stencil.value_mask, ctx->stencil_value_mask, sizeof(e->stencil.value_mask));
    memcpy(e->stencil.write_mask, ctx->stencil_write_mask, sizeof(e->stencil.write_mask));
    e->stencil.clear = ctx->clear_stencil;
    memcpy(e->viewport.box, ctx->viewport, sizeof(ctx->viewport));
    memcpy(e->viewport.range, ctx->depth_range, sizeof(ctx->depth_range));
    e->transform.matrix_mode = f->matrix_mode;
    e->transform.normalize = f->normalize;
    e->transform.rescale_normal = f->rescale_normal;
    e->enable.blend = ctx->blend.enabled;
    e->enable.depth = ctx->depth_test;
    e->enable.stencil = ctx->stencil_test;
    e->enable.scissor = ctx->scissor_test;
    e->enable.cull = ctx->cull_face;
    e->enable.offset_fill = ctx->polygon_offset_fill;
    e->enable.dither = ctx->dither;
    e->enable.multisample = ctx->multisample;
    e->enable.alpha_to_coverage = ctx->sample_alpha_to_coverage;
    e->enable.lighting = f->lighting;
    e->enable.alpha_test = f->alpha_test;
    e->enable.fog = f->fog;
    e->enable.normalize = f->normalize;
    e->enable.rescale_normal = f->rescale_normal;
    e->enable.color_material = f->color_material;
    memcpy(e->enable.texture_1d, f->texture_1d, sizeof(f->texture_1d));
    memcpy(e->enable.texture_2d, f->texture_2d, sizeof(f->texture_2d));
    memcpy(e->enable.texture_3d, f->texture_3d, sizeof(f->texture_3d));
    for (int i = 0; i < FF_LIGHTS; i++)
        e->enable.lights[i] = f->lights[i].enabled;
    memcpy(e->enable.stored, f->stored, sizeof(f->stored));
    e->color_buffer.alpha_test = f->alpha_test;
    e->color_buffer.blend = ctx->blend.enabled;
    e->color_buffer.dither = ctx->dither;
    e->color_buffer.alpha_func = f->alpha_func;
    e->color_buffer.alpha_ref = f->alpha_ref;
    e->color_buffer.blend_state = ctx->blend;
    memcpy(e->color_buffer.blend_color, ctx->blend_color, 16);
    memcpy(e->color_buffer.mask, ctx->color_mask, sizeof(ctx->color_mask));
    memcpy(e->color_buffer.clear, ctx->clear_color, 16);
    e->color_buffer.draw_buffer = ctx->draw_buffer;
    for (int u = 0; u < FF_UNITS; u++) {
        memcpy(e->texture.units[u], ctx->units[u], sizeof(e->texture.units[u]));
        e->texture.env_mode[u] = f->tex_env_mode[u];
        memcpy(e->texture.env_color[u], f->tex_env_color[u], 16);
    }
    e->texture.active = ctx->active_texture;
    e->scissor.test = ctx->scissor_test;
    memcpy(e->scissor.box, ctx->scissor, sizeof(ctx->scissor));
}

static void restore(GlContext *ctx, const AttribEntry *e)
{
    GlFixed *f = &ctx->ff;
    GLbitfield mask = e->mask;
    if (mask & GL_CURRENT_BIT) {
        memcpy(f->current[0], e->current.color, 16);
        memcpy(f->current[1], e->current.normal, 16);
        memcpy(f->current[2], e->current.texcoord, 16);
        memcpy(f->texcoord, e->current.extra, sizeof(f->texcoord));
        f->edge_flag = e->current.edge_flag;
    }
    if (mask & GL_POINT_BIT)
        ctx->point_size = e->point_size;
    if (mask & GL_LINE_BIT)
        ctx->line_width = e->line.width;
    if (mask & GL_POLYGON_BIT) {
        ctx->cull_face = e->polygon.cull_face;
        ctx->polygon_offset_fill = e->polygon.offset_fill;
        ctx->cull_mode = e->polygon.cull_mode;
        ctx->front_face = e->polygon.front_face;
        ctx->polygon_mode = e->polygon.mode;
        ctx->offset_factor = e->polygon.factor;
        ctx->offset_units = e->polygon.units;
    }
    if (mask & GL_LIGHTING_BIT) {
        f->lighting = e->lighting.lighting;
        f->color_material = e->lighting.color_material;
        f->local_viewer = e->lighting.local_viewer;
        f->two_side = e->lighting.two_side;
        f->shade_model = e->lighting.shade_model;
        f->color_material_face = e->lighting.material_face;
        f->color_material_mode = e->lighting.material_mode;
        f->color_control = e->lighting.color_control;
        memcpy(f->model_ambient, e->lighting.model_ambient, 16);
        memcpy(f->material, e->lighting.material, sizeof(f->material));
        memcpy(f->lights, e->lighting.lights, sizeof(f->lights));
    }
    if (mask & GL_FOG_BIT) {
        f->fog = e->fog.enabled;
        f->fog_mode = e->fog.mode;
        f->fog_density = e->fog.density;
        f->fog_start = e->fog.start;
        f->fog_end = e->fog.end;
        memcpy(f->fog_color, e->fog.color, 16);
    }
    if (mask & GL_DEPTH_BUFFER_BIT) {
        ctx->depth_test = e->depth.test;
        ctx->depth_func = e->depth.func;
        ctx->depth_mask = e->depth.mask;
        ctx->clear_depth = e->depth.clear;
    }
    if (mask & GL_STENCIL_BUFFER_BIT) {
        ctx->stencil_test = e->stencil.test;
        memcpy(ctx->stencil_func, e->stencil.func, sizeof(e->stencil.func));
        memcpy(ctx->stencil_fail, e->stencil.fail, sizeof(e->stencil.fail));
        memcpy(ctx->stencil_zfail, e->stencil.zfail, sizeof(e->stencil.zfail));
        memcpy(ctx->stencil_zpass, e->stencil.zpass, sizeof(e->stencil.zpass));
        memcpy(ctx->stencil_ref, e->stencil.ref, sizeof(e->stencil.ref));
        memcpy(ctx->stencil_value_mask, e->stencil.value_mask, sizeof(e->stencil.value_mask));
        memcpy(ctx->stencil_write_mask, e->stencil.write_mask, sizeof(e->stencil.write_mask));
        ctx->clear_stencil = e->stencil.clear;
    }
    if (mask & GL_VIEWPORT_BIT) {
        memcpy(ctx->viewport, e->viewport.box, sizeof(ctx->viewport));
        memcpy(ctx->depth_range, e->viewport.range, sizeof(ctx->depth_range));
    }
    if (mask & GL_TRANSFORM_BIT) {
        f->matrix_mode = e->transform.matrix_mode;
        f->normalize = e->transform.normalize;
        f->rescale_normal = e->transform.rescale_normal;
    }
    if (mask & GL_ENABLE_BIT) {
        ctx->blend.enabled = e->enable.blend;
        ctx->depth_test = e->enable.depth;
        ctx->stencil_test = e->enable.stencil;
        ctx->scissor_test = e->enable.scissor;
        ctx->cull_face = e->enable.cull;
        ctx->polygon_offset_fill = e->enable.offset_fill;
        ctx->dither = e->enable.dither;
        ctx->multisample = e->enable.multisample;
        ctx->sample_alpha_to_coverage = e->enable.alpha_to_coverage;
        f->lighting = e->enable.lighting;
        f->alpha_test = e->enable.alpha_test;
        f->fog = e->enable.fog;
        f->normalize = e->enable.normalize;
        f->rescale_normal = e->enable.rescale_normal;
        f->color_material = e->enable.color_material;
        memcpy(f->texture_1d, e->enable.texture_1d, sizeof(f->texture_1d));
        memcpy(f->texture_2d, e->enable.texture_2d, sizeof(f->texture_2d));
        memcpy(f->texture_3d, e->enable.texture_3d, sizeof(f->texture_3d));
        for (int i = 0; i < FF_LIGHTS; i++)
            f->lights[i].enabled = e->enable.lights[i];
        memcpy(f->stored, e->enable.stored, sizeof(f->stored));
    }
    if (mask & GL_COLOR_BUFFER_BIT) {
        f->alpha_test = e->color_buffer.alpha_test;
        ctx->dither = e->color_buffer.dither;
        f->alpha_func = e->color_buffer.alpha_func;
        f->alpha_ref = e->color_buffer.alpha_ref;
        ctx->blend = e->color_buffer.blend_state;
        ctx->blend.enabled = e->color_buffer.blend;
        memcpy(ctx->blend_color, e->color_buffer.blend_color, 16);
        memcpy(ctx->color_mask, e->color_buffer.mask, sizeof(ctx->color_mask));
        memcpy(ctx->clear_color, e->color_buffer.clear, 16);
        ctx->draw_buffer = e->color_buffer.draw_buffer;
    }
    if (mask & GL_TEXTURE_BIT) {
        for (int u = 0; u < FF_UNITS; u++) {
            memcpy(ctx->units[u], e->texture.units[u], sizeof(ctx->units[u]));
            f->tex_env_mode[u] = e->texture.env_mode[u];
            memcpy(f->tex_env_color[u], e->texture.env_color[u], 16);
        }
        ctx->active_texture = e->texture.active;
    }
    if (mask & GL_SCISSOR_BIT) {
        ctx->scissor_test = e->scissor.test;
        memcpy(ctx->scissor, e->scissor.box, sizeof(ctx->scissor));
    }
    /* what was written has to reach the host. */
    ctx->dirty |= DIRTY_ALL;
    f->dirty = TRUE;
}

GLAPI void APIENTRY glPushAttrib(GLbitfield mask)
{
    GL_CONTEXT_OR_RETURN();
    if (ctx->attrib_depth >= ATTRIB_DEPTH) {
        gl_error(ctx, GL_STACK_OVERFLOW);
        return;
    }
    if (!ctx->attrib_stack) {
        ctx->attrib_stack = gl_alloc(sizeof(AttribEntry) * ATTRIB_DEPTH);
        if (!ctx->attrib_stack) {
            gl_error(ctx, GL_OUT_OF_MEMORY);
            return;
        }
    }
    save(ctx, (AttribEntry *)ctx->attrib_stack + ctx->attrib_depth++, mask);
}

GLAPI void APIENTRY glPopAttrib(void)
{
    GL_CONTEXT_OR_RETURN();
    if (ctx->attrib_depth == 0) {
        gl_error(ctx, GL_STACK_UNDERFLOW);
        return;
    }
    restore(ctx, (AttribEntry *)ctx->attrib_stack + --ctx->attrib_depth);
}

GLAPI void APIENTRY glPushClientAttrib(GLbitfield mask)
{
    GL_CONTEXT_OR_RETURN();
    if (ctx->client_attrib_depth >= ATTRIB_DEPTH) {
        gl_error(ctx, GL_STACK_OVERFLOW);
        return;
    }
    if (!ctx->client_attrib_stack) {
        ctx->client_attrib_stack = gl_alloc(sizeof(ClientEntry) * ATTRIB_DEPTH);
        if (!ctx->client_attrib_stack) {
            gl_error(ctx, GL_OUT_OF_MEMORY);
            return;
        }
    }
    ClientEntry *e = (ClientEntry *)ctx->client_attrib_stack + ctx->client_attrib_depth++;
    e->mask = mask;
    e->array = *ctx->vao;
    e->array_buffer = ctx->array_buffer;
    e->client_unit = ctx->ff.client_unit;
    memcpy(e->unused, ctx->ff.unused_arrays, sizeof(e->unused));
    e->pack[0] = ctx->pack_row_length;
    e->pack[1] = ctx->pack_skip_rows;
    e->pack[2] = ctx->pack_skip_pixels;
    e->unpack[0] = ctx->unpack_row_length;
    e->unpack[1] = ctx->unpack_skip_rows;
    e->unpack[2] = ctx->unpack_skip_pixels;
    e->unpack[3] = ctx->unpack_image_height;
    e->unpack[4] = ctx->unpack_skip_images;
    e->pack_alignment = ctx->pack_alignment;
    e->unpack_alignment = ctx->unpack_alignment;
}

GLAPI void APIENTRY glPopClientAttrib(void)
{
    GL_CONTEXT_OR_RETURN();
    if (ctx->client_attrib_depth == 0) {
        gl_error(ctx, GL_STACK_UNDERFLOW);
        return;
    }
    const ClientEntry *e = (const ClientEntry *)ctx->client_attrib_stack + --ctx->client_attrib_depth;
    if (e->mask & GL_CLIENT_VERTEX_ARRAY_BIT) {
        /* the arrays go back into the vertex array object that is bound, with the name it has. */
        GLuint name = ctx->vao->name;
        *ctx->vao = e->array;
        ctx->vao->name = name;
        ctx->array_buffer = e->array_buffer;
        ctx->ff.client_unit = e->client_unit;
        memcpy(ctx->ff.unused_arrays, e->unused, sizeof(e->unused));
        ctx->dirty |= DIRTY_VERTEX | DIRTY_PIPELINE;
    }
    if (e->mask & GL_CLIENT_PIXEL_STORE_BIT) {
        ctx->pack_row_length = e->pack[0];
        ctx->pack_skip_rows = e->pack[1];
        ctx->pack_skip_pixels = e->pack[2];
        ctx->unpack_row_length = e->unpack[0];
        ctx->unpack_skip_rows = e->unpack[1];
        ctx->unpack_skip_pixels = e->unpack[2];
        ctx->unpack_image_height = e->unpack[3];
        ctx->unpack_skip_images = e->unpack[4];
        ctx->pack_alignment = e->pack_alignment;
        ctx->unpack_alignment = e->unpack_alignment;
    }
}
