/*
 * fixed state: errors, capabilities, blending, depth, stencil, rasterizer,
 * viewport and scissor, pixel storage, and the queries reading them back.
 * setters only mark what changed; draws send it.
 */
#include "opengl32.h"

static int hint_index(GLenum target);

static const char g_extensions[] =
    "GL_ARB_framebuffer_object GL_EXT_framebuffer_object GL_EXT_framebuffer_blit GL_EXT_framebuffer_multisample "
    "GL_EXT_packed_depth_stencil GL_ARB_vertex_array_object GL_ARB_vertex_buffer_object GL_ARB_pixel_buffer_object "
    "GL_ARB_map_buffer_range GL_ARB_copy_buffer GL_ARB_texture_non_power_of_two GL_ARB_texture_rectangle "
    "GL_EXT_bgra GL_EXT_texture_format_BGRA8888 GL_ARB_texture_float GL_ARB_half_float_pixel GL_ARB_texture_rg "
    "GL_ARB_depth_texture GL_EXT_texture_edge_clamp GL_ARB_texture_border_clamp GL_ARB_texture_mirrored_repeat "
    "GL_EXT_texture_filter_anisotropic GL_ARB_shader_objects GL_ARB_vertex_shader GL_ARB_fragment_shader "
    "GL_ARB_shading_language_100 GL_ARB_explicit_attrib_location GL_EXT_blend_func_separate GL_EXT_blend_minmax "
    "GL_EXT_blend_subtract GL_EXT_blend_equation_separate GL_EXT_blend_color GL_ARB_draw_instanced "
    "GL_ARB_instanced_arrays GL_ARB_draw_elements_base_vertex GL_EXT_texture_sRGB GL_ARB_framebuffer_sRGB "
    "GL_ARB_texture_storage GL_EXT_texture_compression_s3tc GL_ARB_texture_compression GL_ARB_multitexture "
    "GL_ARB_depth_buffer_float GL_EXT_stencil_wrap GL_EXT_draw_buffers2 GL_ARB_draw_buffers "
    "GL_ARB_occlusion_query GL_ARB_get_program_binary GL_KHR_debug";

static const char *extension_list[128];
static GLint extension_count;

static void split_extensions(void)
{
    if (extension_count)
        return;
    static char copy[sizeof(g_extensions)];
    memcpy(copy, g_extensions, sizeof(copy));
    char *p = copy;
    while (*p && extension_count < 128) {
        extension_list[extension_count++] = p;
        while (*p && *p != ' ')
            p++;
        if (*p)
            *p++ = 0;
    }
}

void gl_error(GlContext *ctx, GLenum error)
{
    if (!ctx->error)
        ctx->error = error;
}

GLAPI GLenum APIENTRY glGetError(void)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN(GL_INVALID_OPERATION);
    GLenum e = ctx->error;
    ctx->error = GL_NO_ERROR;
    return e;
}

void gl_context_init(GlContext *ctx)
{
    ctx->vao = &ctx->default_vao;
    ctx->depth_range[1] = 1.0;
    ctx->clear_depth = 1.0;
    ctx->blend.src_rgb = ctx->blend.src_alpha = GL_ONE;
    ctx->blend.dst_rgb = ctx->blend.dst_alpha = GL_ZERO;
    ctx->blend.eq_rgb = ctx->blend.eq_alpha = GL_FUNC_ADD;
    for (int i = 0; i < 4; i++)
        ctx->color_mask[i] = GL_TRUE;
    ctx->depth_mask = GL_TRUE;
    ctx->depth_func = GL_LESS;
    for (int f = 0; f < 2; f++) {
        ctx->stencil_func[f] = GL_ALWAYS;
        ctx->stencil_value_mask[f] = ctx->stencil_write_mask[f] = ~0u;
        ctx->stencil_fail[f] = ctx->stencil_zfail[f] = ctx->stencil_zpass[f] = GL_KEEP;
    }
    ctx->cull_mode = GL_BACK;
    ctx->front_face = GL_CCW;
    ctx->polygon_mode = GL_FILL;
    ctx->dither = TRUE;
    ctx->multisample = TRUE;
    ctx->line_width = ctx->point_size = 1.0f;
    ctx->point_size_min = 0.0f;
    ctx->point_size_max = 8192.0f;
    ctx->point_fade_threshold = 1.0f;
    ctx->point_attenuation[0] = 1.0f;
    ctx->point_sprite_origin = GL_UPPER_LEFT;
    ctx->pack_alignment = ctx->unpack_alignment = 4;
    for (int i = 0; i < MIME_GPU_MAX_ATTRIBS; i++)
        ctx->generic[i][3] = 1.0f;
    ctx->draw_buffer = ctx->read_buffer = GL_BACK;
    ctx->dirty = DIRTY_ALL;
    ctx->swap_interval = 1;
    for (int i = 0; i < 8; i++)
        ctx->hints[i] = GL_DONT_CARE;
    gl_ff_init(ctx);
    gl_raster_init(ctx);
}

/* ---------------------------------------------------------------- capabilities */

static BOOL *capability(GlContext *ctx, GLenum cap, uint32_t *dirty)
{
    switch (cap) {
    case GL_BLEND:
        *dirty = DIRTY_PIPELINE;
        return &ctx->blend.enabled;
    case GL_DEPTH_TEST:
        *dirty = DIRTY_DEPTH;
        return &ctx->depth_test;
    case GL_STENCIL_TEST:
        *dirty = DIRTY_DEPTH;
        return &ctx->stencil_test;
    case GL_SCISSOR_TEST:
        *dirty = DIRTY_SCISSOR;
        return &ctx->scissor_test;
    case GL_CULL_FACE:
        *dirty = DIRTY_RASTER;
        return &ctx->cull_face;
    case GL_POLYGON_OFFSET_FILL:
        *dirty = DIRTY_RASTER;
        return &ctx->polygon_offset_fill;
    case GL_DEPTH_CLAMP:
        *dirty = DIRTY_RASTER;
        return &ctx->depth_clamp;
    case GL_DITHER:
        *dirty = 0;
        return &ctx->dither;
    case GL_MULTISAMPLE:
        *dirty = 0;
        return &ctx->multisample;
    case GL_SAMPLE_ALPHA_TO_COVERAGE:
        *dirty = DIRTY_PIPELINE;
        return &ctx->sample_alpha_to_coverage;
    case GL_PROGRAM_POINT_SIZE:
        *dirty = 0;
        return &ctx->program_point_size;
    case GL_PRIMITIVE_RESTART_FIXED_INDEX:
    case GL_PRIMITIVE_RESTART:
        *dirty = 0;
        return &ctx->primitive_restart;
    case GL_FRAMEBUFFER_SRGB:
        *dirty = 0;
        return &ctx->framebuffer_srgb;
    default:
        return nullptr;
    }
}

static void set_capability(GLenum cap, BOOL on)
{
    GL_CONTEXT_OR_RETURN();
    uint32_t dirty = 0;
    BOOL *p = capability(ctx, cap, &dirty);
    if (!p) {
        if (!gl_ff_capability(ctx, cap, on))
            gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (*p != on) {
        *p = on;
        ctx->dirty |= dirty;
    }
}

GLAPI void APIENTRY glEnable(GLenum cap)
{
    set_capability(cap, TRUE);
}

GLAPI void APIENTRY glDisable(GLenum cap)
{
    set_capability(cap, FALSE);
}

GLAPI GLboolean APIENTRY glIsEnabled(GLenum cap)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    uint32_t dirty;
    BOOL *p = capability(ctx, cap, &dirty);
    if (!p) {
        BOOL known;
        BOOL on = gl_ff_is_enabled(ctx, cap, &known);
        if (!known)
            gl_error(ctx, GL_INVALID_ENUM);
        return on ? GL_TRUE : GL_FALSE;
    }
    return *p ? GL_TRUE : GL_FALSE;
}

GLAPI void APIENTRY glEnablei(GLenum cap, GLuint index)
{
    glEnable(cap);
}

GLAPI void APIENTRY glDisablei(GLenum cap, GLuint index)
{
    glDisable(cap);
}

GLAPI GLboolean APIENTRY glIsEnabledi(GLenum cap, GLuint index)
{
    return glIsEnabled(cap);
}

/* ---------------------------------------------------------------- blending */

static BOOL blend_factor_valid(GLenum f, BOOL source)
{
    switch (f) {
    case GL_ZERO:
    case GL_ONE:
    case GL_SRC_COLOR:
    case GL_ONE_MINUS_SRC_COLOR:
    case GL_DST_COLOR:
    case GL_ONE_MINUS_DST_COLOR:
    case GL_SRC_ALPHA:
    case GL_ONE_MINUS_SRC_ALPHA:
    case GL_DST_ALPHA:
    case GL_ONE_MINUS_DST_ALPHA:
    case GL_CONSTANT_COLOR:
    case GL_ONE_MINUS_CONSTANT_COLOR:
    case GL_CONSTANT_ALPHA:
    case GL_ONE_MINUS_CONSTANT_ALPHA:
    case GL_SRC1_COLOR:
    case GL_ONE_MINUS_SRC1_COLOR:
    case GL_SRC1_ALPHA:
    case GL_ONE_MINUS_SRC1_ALPHA:
        return TRUE;
    case GL_SRC_ALPHA_SATURATE:
        return source;
    default:
        return FALSE;
    }
}

static BOOL blend_equation_valid(GLenum e)
{
    return e == GL_FUNC_ADD || e == GL_FUNC_SUBTRACT || e == GL_FUNC_REVERSE_SUBTRACT || e == GL_MIN || e == GL_MAX;
}

static BOOL compare_valid(GLenum f)
{
    return f >= GL_NEVER && f <= GL_ALWAYS;
}

static BOOL stencil_op_valid(GLenum o)
{
    return o == GL_KEEP || o == GL_ZERO || o == GL_REPLACE || o == GL_INCR || o == GL_DECR || o == GL_INVERT ||
           o == GL_INCR_WRAP || o == GL_DECR_WRAP;
}

GLAPI void APIENTRY glBlendFunc(GLenum src, GLenum dst)
{
    GL_CONTEXT_OR_RETURN();
    if (!blend_factor_valid(src, TRUE) || !blend_factor_valid(dst, FALSE)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->blend.src_rgb = ctx->blend.src_alpha = src;
    ctx->blend.dst_rgb = ctx->blend.dst_alpha = dst;
    ctx->dirty |= DIRTY_PIPELINE;
}

GLAPI void APIENTRY glBlendFuncSeparate(GLenum src_rgb, GLenum dst_rgb, GLenum src_alpha, GLenum dst_alpha)
{
    GL_CONTEXT_OR_RETURN();
    if (!blend_factor_valid(src_rgb, TRUE) || !blend_factor_valid(dst_rgb, FALSE) || !blend_factor_valid(src_alpha, TRUE) ||
        !blend_factor_valid(dst_alpha, FALSE)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->blend.src_rgb = src_rgb;
    ctx->blend.dst_rgb = dst_rgb;
    ctx->blend.src_alpha = src_alpha;
    ctx->blend.dst_alpha = dst_alpha;
    ctx->dirty |= DIRTY_PIPELINE;
}

GLAPI void APIENTRY glBlendEquation(GLenum mode)
{
    GL_CONTEXT_OR_RETURN();
    if (!blend_equation_valid(mode)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->blend.eq_rgb = ctx->blend.eq_alpha = mode;
    ctx->dirty |= DIRTY_PIPELINE;
}

GLAPI void APIENTRY glBlendEquationSeparate(GLenum rgb, GLenum alpha)
{
    GL_CONTEXT_OR_RETURN();
    if (!blend_equation_valid(rgb) || !blend_equation_valid(alpha)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->blend.eq_rgb = rgb;
    ctx->blend.eq_alpha = alpha;
    ctx->dirty |= DIRTY_PIPELINE;
}

GLAPI void APIENTRY glBlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    GL_CONTEXT_OR_RETURN();
    ctx->blend_color[0] = r;
    ctx->blend_color[1] = g;
    ctx->blend_color[2] = b;
    ctx->blend_color[3] = a;
    ctx->dirty |= DIRTY_BLEND_COLOR;
}

GLAPI void APIENTRY glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    GL_CONTEXT_OR_RETURN();
    ctx->color_mask[0] = r;
    ctx->color_mask[1] = g;
    ctx->color_mask[2] = b;
    ctx->color_mask[3] = a;
    ctx->dirty |= DIRTY_PIPELINE;
}

GLAPI void APIENTRY glColorMaski(GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    glColorMask(r, g, b, a);
}

/* ---------------------------------------------------------------- depth and stencil */

GLAPI void APIENTRY glDepthFunc(GLenum func)
{
    GL_CONTEXT_OR_RETURN();
    if (!compare_valid(func)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->depth_func = func;
    ctx->dirty |= DIRTY_DEPTH;
}

GLAPI void APIENTRY glDepthMask(GLboolean flag)
{
    GL_CONTEXT_OR_RETURN();
    ctx->depth_mask = flag;
    ctx->dirty |= DIRTY_DEPTH;
}

GLAPI void APIENTRY glDepthRange(GLdouble n, GLdouble f)
{
    GL_CONTEXT_OR_RETURN();
    ctx->depth_range[0] = n < 0 ? 0 : n > 1 ? 1 : n;
    ctx->depth_range[1] = f < 0 ? 0 : f > 1 ? 1 : f;
    ctx->dirty |= DIRTY_VIEWPORT;
}

GLAPI void APIENTRY glDepthRangef(GLfloat n, GLfloat f)
{
    glDepthRange(n, f);
}

GLAPI void APIENTRY glClearDepth(GLdouble d)
{
    GL_CONTEXT_OR_RETURN();
    ctx->clear_depth = d < 0 ? 0 : d > 1 ? 1 : d;
}

GLAPI void APIENTRY glClearDepthf(GLfloat d)
{
    glClearDepth(d);
}

GLAPI void APIENTRY glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    GL_CONTEXT_OR_RETURN();
    ctx->clear_color[0] = r;
    ctx->clear_color[1] = g;
    ctx->clear_color[2] = b;
    ctx->clear_color[3] = a;
}

GLAPI void APIENTRY glClearStencil(GLint s)
{
    GL_CONTEXT_OR_RETURN();
    ctx->clear_stencil = s;
}

static void stencil_faces(GLenum face, int *first, int *last)
{
    *first = face == GL_BACK ? 1 : 0;
    *last = face == GL_FRONT ? 0 : 1;
}

GLAPI void APIENTRY glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask)
{
    GL_CONTEXT_OR_RETURN();
    if ((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) || !compare_valid(func)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    int a, b;
    stencil_faces(face, &a, &b);
    for (int f = a; f <= b; f++) {
        ctx->stencil_func[f] = func;
        ctx->stencil_ref[f] = ref;
        ctx->stencil_value_mask[f] = mask;
    }
    ctx->dirty |= DIRTY_DEPTH;
}

GLAPI void APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
    glStencilFuncSeparate(GL_FRONT_AND_BACK, func, ref, mask);
}

GLAPI void APIENTRY glStencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass)
{
    GL_CONTEXT_OR_RETURN();
    if ((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) || !stencil_op_valid(fail) ||
        !stencil_op_valid(zfail) || !stencil_op_valid(zpass)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    int a, b;
    stencil_faces(face, &a, &b);
    for (int f = a; f <= b; f++) {
        ctx->stencil_fail[f] = fail;
        ctx->stencil_zfail[f] = zfail;
        ctx->stencil_zpass[f] = zpass;
    }
    ctx->dirty |= DIRTY_DEPTH;
}

GLAPI void APIENTRY glStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
    glStencilOpSeparate(GL_FRONT_AND_BACK, fail, zfail, zpass);
}

GLAPI void APIENTRY glStencilMaskSeparate(GLenum face, GLuint mask)
{
    GL_CONTEXT_OR_RETURN();
    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    int a, b;
    stencil_faces(face, &a, &b);
    for (int f = a; f <= b; f++)
        ctx->stencil_write_mask[f] = mask;
    ctx->dirty |= DIRTY_DEPTH;
}

GLAPI void APIENTRY glStencilMask(GLuint mask)
{
    glStencilMaskSeparate(GL_FRONT_AND_BACK, mask);
}

/* ---------------------------------------------------------------- rasterizer */

GLAPI void APIENTRY glCullFace(GLenum mode)
{
    GL_CONTEXT_OR_RETURN();
    if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->cull_mode = mode;
    ctx->dirty |= DIRTY_RASTER;
}

GLAPI void APIENTRY glFrontFace(GLenum mode)
{
    GL_CONTEXT_OR_RETURN();
    if (mode != GL_CW && mode != GL_CCW) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->front_face = mode;
    ctx->dirty |= DIRTY_RASTER;
}

GLAPI void APIENTRY glPolygonMode(GLenum face, GLenum mode)
{
    GL_CONTEXT_OR_RETURN();
    if ((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) ||
        (mode != GL_POINT && mode != GL_LINE && mode != GL_FILL)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->polygon_mode = mode;
    ctx->dirty |= DIRTY_RASTER;
}

GLAPI void APIENTRY glPolygonOffset(GLfloat factor, GLfloat units)
{
    GL_CONTEXT_OR_RETURN();
    ctx->offset_factor = factor;
    ctx->offset_units = units;
    ctx->dirty |= DIRTY_RASTER;
}

GLAPI void APIENTRY glLineWidth(GLfloat width)
{
    GL_CONTEXT_OR_RETURN();
    if (!(width > 0)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    ctx->line_width = width;
}

GLAPI void APIENTRY glPointSize(GLfloat size)
{
    GL_CONTEXT_OR_RETURN();
    if (!(size > 0)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    ctx->point_size = size;
}

/*
 * the point parameters of OpenGL 1.4 and of the extensions before it. the size limits clamp the size a draw uses and
 * the rest is kept for queries: the distance attenuation of points is not applied.
 */
static void point_parameters(GlContext *ctx, GLenum pname, const GLfloat *v)
{
    switch (pname) {
    case GL_POINT_SIZE_MIN:
        if (v[0] < 0)
            gl_error(ctx, GL_INVALID_VALUE);
        else
            ctx->point_size_min = v[0];
        break;
    case GL_POINT_SIZE_MAX:
        if (v[0] < 0)
            gl_error(ctx, GL_INVALID_VALUE);
        else
            ctx->point_size_max = v[0];
        break;
    case GL_POINT_FADE_THRESHOLD_SIZE:
        if (v[0] < 0)
            gl_error(ctx, GL_INVALID_VALUE);
        else
            ctx->point_fade_threshold = v[0];
        break;
    case GL_POINT_DISTANCE_ATTENUATION:
        memcpy(ctx->point_attenuation, v, sizeof(ctx->point_attenuation));
        break;
    case GL_POINT_SPRITE_COORD_ORIGIN:
        if ((GLenum)v[0] != GL_UPPER_LEFT && (GLenum)v[0] != GL_LOWER_LEFT)
            gl_error(ctx, GL_INVALID_VALUE);
        else
            ctx->point_sprite_origin = (GLenum)v[0];
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
    }
}

GLfloat gl_point_size(const GlContext *ctx)
{
    GLfloat size = ctx->point_size;
    if (size < ctx->point_size_min)
        size = ctx->point_size_min;
    if (size > ctx->point_size_max)
        size = ctx->point_size_max;
    return size;
}

GLAPI void APIENTRY glPointParameterf(GLenum pname, GLfloat value)
{
    GL_CONTEXT_OR_RETURN();
    point_parameters(ctx, pname, &value);
}

GLAPI void APIENTRY glPointParameterfv(GLenum pname, const GLfloat *values)
{
    GL_CONTEXT_OR_RETURN();
    if (!values)
        return;
    point_parameters(ctx, pname, values);
}

GLAPI void APIENTRY glPointParameteri(GLenum pname, GLint value)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v = (GLfloat)value;
    point_parameters(ctx, pname, &v);
}

GLAPI void APIENTRY glPointParameteriv(GLenum pname, const GLint *values)
{
    GL_CONTEXT_OR_RETURN();
    if (!values)
        return;
    GLfloat v[3] = {(GLfloat)values[0], pname == GL_POINT_DISTANCE_ATTENUATION ? (GLfloat)values[1] : 0,
                    pname == GL_POINT_DISTANCE_ATTENUATION ? (GLfloat)values[2] : 0};
    point_parameters(ctx, pname, v);
}

GLAPI void APIENTRY glPointParameterfARB(GLenum pname, GLfloat value)
{
    glPointParameterf(pname, value);
}

GLAPI void APIENTRY glPointParameterfvARB(GLenum pname, const GLfloat *values)
{
    glPointParameterfv(pname, values);
}

GLAPI void APIENTRY glPointParameterfEXT(GLenum pname, GLfloat value)
{
    glPointParameterf(pname, value);
}

GLAPI void APIENTRY glPointParameterfvEXT(GLenum pname, const GLfloat *values)
{
    glPointParameterfv(pname, values);
}

GLAPI void APIENTRY glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    GL_CONTEXT_OR_RETURN();
    if (w < 0 || h < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    ctx->viewport[0] = x;
    ctx->viewport[1] = y;
    ctx->viewport[2] = w;
    ctx->viewport[3] = h;
    ctx->dirty |= DIRTY_VIEWPORT;
}

GLAPI void APIENTRY glScissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
    GL_CONTEXT_OR_RETURN();
    if (w < 0 || h < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    ctx->scissor[0] = x;
    ctx->scissor[1] = y;
    ctx->scissor[2] = w;
    ctx->scissor[3] = h;
    ctx->dirty |= DIRTY_SCISSOR;
}

static int hint_index(GLenum target)
{
    switch (target) {
    case GL_PERSPECTIVE_CORRECTION_HINT:
        return 0;
    case GL_POINT_SMOOTH_HINT:
        return 1;
    case GL_LINE_SMOOTH_HINT:
        return 2;
    case GL_POLYGON_SMOOTH_HINT:
        return 3;
    case GL_FOG_HINT:
        return 4;
    case GL_GENERATE_MIPMAP_HINT:
        return 5;
    case GL_TEXTURE_COMPRESSION_HINT:
        return 6;
    case GL_FRAGMENT_SHADER_DERIVATIVE_HINT:
        return 7;
    default:
        return -1;
    }
}

GLAPI void APIENTRY glHint(GLenum target, GLenum mode)
{
    GL_CONTEXT_OR_RETURN();
    int i = hint_index(target);
    if (i < 0 || (mode != GL_DONT_CARE && mode != GL_FASTEST && mode != GL_NICEST)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->hints[i] = mode;
}

GLAPI void APIENTRY glPixelStorei(GLenum pname, GLint param)
{
    GL_CONTEXT_OR_RETURN();
    /* a negative length or skip is no value, and nothing is changed. */
    switch (pname) {
    case GL_UNPACK_ROW_LENGTH:
    case GL_UNPACK_SKIP_ROWS:
    case GL_UNPACK_SKIP_PIXELS:
    case GL_UNPACK_IMAGE_HEIGHT:
    case GL_UNPACK_SKIP_IMAGES:
    case GL_PACK_ROW_LENGTH:
    case GL_PACK_SKIP_ROWS:
    case GL_PACK_SKIP_PIXELS:
    case GL_PACK_IMAGE_HEIGHT:
    case GL_PACK_SKIP_IMAGES:
        if (param < 0) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
        break;
    default:
        break;
    }
    switch (pname) {
    case GL_PACK_ALIGNMENT:
        if (param != 1 && param != 2 && param != 4 && param != 8) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
        ctx->pack_alignment = param;
        break;
    case GL_UNPACK_LSB_FIRST:
        ctx->unpack_lsb_first = param != 0;
        break;
    case GL_UNPACK_SWAP_BYTES:
        ctx->unpack_swap_bytes = param != 0;
        break;
    case GL_UNPACK_ALIGNMENT:
        if (param != 1 && param != 2 && param != 4 && param != 8) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
        ctx->unpack_alignment = param;
        break;
    case GL_UNPACK_ROW_LENGTH:
        ctx->unpack_row_length = param;
        break;
    case GL_UNPACK_SKIP_ROWS:
        ctx->unpack_skip_rows = param;
        break;
    case GL_UNPACK_SKIP_PIXELS:
        ctx->unpack_skip_pixels = param;
        break;
    case GL_UNPACK_IMAGE_HEIGHT:
        ctx->unpack_image_height = param;
        break;
    case GL_UNPACK_SKIP_IMAGES:
        ctx->unpack_skip_images = param;
        break;
    case GL_PACK_ROW_LENGTH:
        ctx->pack_row_length = param;
        break;
    case GL_PACK_SKIP_ROWS:
        ctx->pack_skip_rows = param;
        break;
    case GL_PACK_SKIP_PIXELS:
        ctx->pack_skip_pixels = param;
        break;
    case GL_PACK_SWAP_BYTES:
    case GL_PACK_LSB_FIRST:
    case GL_PACK_IMAGE_HEIGHT:
    case GL_PACK_SKIP_IMAGES:
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
}

GLAPI void APIENTRY glPixelStoref(GLenum pname, GLfloat param)
{
    glPixelStorei(pname, (GLint)param);
}

/* ---------------------------------------------------------------- flushing */

/* a single buffered window shows what was drawn when the program flushes. */
static void present_single_buffered(GlContext *ctx)
{
    if (ctx->surface && !(ctx->pfd.dwFlags & PFD_DOUBLEBUFFER))
        wglSwapBuffers(ctx->dc);
}

GLAPI void APIENTRY glFlush(void)
{
    GL_CONTEXT_OR_RETURN();
    gl_device_lock();
    gl_emit(MIME_GPU_FLUSH, sizeof(MimeGpuCmd));
    gl_publish();
    gl_device_unlock();
    present_single_buffered(ctx);
}

GLAPI void APIENTRY glFinish(void)
{
    GL_CONTEXT_OR_RETURN();
    gl_device_lock();
    gl_flush_clears(ctx);
    uint64_t f = gl_fence();
    gl_device_unlock();
    gl_wait(f);
    present_single_buffered(ctx);
}

/* ---------------------------------------------------------------- strings */

#if defined(__aarch64__)
/* the reference machine's gpu. */
#define GL_VENDOR_TEXT "Qualcomm"
#define GL_RENDERER_TEXT "Qualcomm Adreno X1-85"
#define GL_BUILD_TEXT "31.0.63.7"
#else
/* on x64 the host gpu is real hardware: its vendor decides what the strings name. */
#define GL_BUILD_TEXT "31.0.101.5333"
static const char *gl_vendor_text(void)
{
    uint32_t regs[4];
    __asm__ volatile("cpuid" : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3]) : "a"(0), "c"(0));
    if (*(const uint32_t *)"Auth" == regs[1] && *(const uint32_t *)"enti" == regs[3])
        return "AuthenticAMD";
    return "Intel";
}

static const char *gl_renderer_text(void)
{
    const char *vendor = gl_vendor_text();
    return vendor[0] == 'A' ? "AMD Radeon(TM) Graphics" : "Intel(R) Arc(TM) Graphics";
}
#endif

GLAPI const GLubyte *APIENTRY glGetString(GLenum name)
{
    GL_CONTEXT_OR_RETURN(nullptr);
    switch (name) {
#ifdef __aarch64__
    case GL_VENDOR:
        return (const GLubyte *)GL_VENDOR_TEXT;
    case GL_RENDERER:
        return (const GLubyte *)GL_RENDERER_TEXT;
#else
    case GL_VENDOR:
        return (const GLubyte *)gl_vendor_text();
    case GL_RENDERER:
        return (const GLubyte *)gl_renderer_text();
#endif
    case GL_VERSION:
        if (ctx->major == 4)
            return (const GLubyte *)(ctx->minor ? "4.1 - Build " GL_BUILD_TEXT : "4.0 - Build " GL_BUILD_TEXT);
        if (ctx->major == 3 && ctx->minor >= 2)
            return (const GLubyte *)(ctx->minor == 3 ? "3.3 - Build " GL_BUILD_TEXT : "3.2 - Build " GL_BUILD_TEXT);
        return (const GLubyte *)"3.0 - Build " GL_BUILD_TEXT;
    case GL_SHADING_LANGUAGE_VERSION:
        if (ctx->major == 4)
            return (const GLubyte *)"4.10";
        if (ctx->major == 3 && ctx->minor >= 2)
            return (const GLubyte *)(ctx->minor == 3 ? "3.30" : "1.50");
        return (const GLubyte *)"1.30";
    case GL_EXTENSIONS:
        if (ctx->profile == WGL_CONTEXT_CORE_PROFILE_BIT_ARB) {
            gl_error(ctx, GL_INVALID_ENUM);
            return nullptr;
        }
        return (const GLubyte *)g_extensions;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        return nullptr;
    }
}

GLAPI const GLubyte *APIENTRY glGetStringi(GLenum name, GLuint index)
{
    GL_CONTEXT_OR_RETURN(nullptr);
    split_extensions();
    if (name != GL_EXTENSIONS || index >= (GLuint)extension_count) {
        gl_error(ctx, name != GL_EXTENSIONS ? GL_INVALID_ENUM : GL_INVALID_VALUE);
        return nullptr;
    }
    return (const GLubyte *)extension_list[index];
}

/* ---------------------------------------------------------------- queries */

static GLint gl_enum_of_bool(BOOL b)
{
    return b ? GL_TRUE : GL_FALSE;
}

/* values as doubles; the typed getters convert. returns the count, 0 if pname is unknown. */
static int get_values(GlContext *ctx, GLenum pname, GLdouble *v, BOOL *colors)
{
    *colors = FALSE;
    switch (pname) {
    case GL_MAJOR_VERSION:
        v[0] = ctx->major;
        return 1;
    case GL_MINOR_VERSION:
        v[0] = ctx->minor;
        return 1;
    case GL_NUM_EXTENSIONS:
        split_extensions();
        v[0] = extension_count;
        return 1;
    case GL_CONTEXT_PROFILE_MASK:
        v[0] = ctx->profile == WGL_CONTEXT_CORE_PROFILE_BIT_ARB ? GL_CONTEXT_CORE_PROFILE_BIT
                                                                : GL_CONTEXT_COMPATIBILITY_PROFILE_BIT;
        return 1;
    case GL_CONTEXT_FLAGS:
        v[0] = 0;
        return 1;
    case GL_MAX_TEXTURE_SIZE:
    case GL_MAX_RENDERBUFFER_SIZE:
    case GL_MAX_RECTANGLE_TEXTURE_SIZE:
    case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
        v[0] = 16384;
        return 1;
    case GL_MAX_3D_TEXTURE_SIZE:
        v[0] = 2048;
        return 1;
    case GL_MAX_ARRAY_TEXTURE_LAYERS:
        v[0] = 2048;
        return 1;
    case GL_MAX_VIEWPORT_DIMS:
        v[0] = v[1] = 16384;
        return 2;
    case GL_MAX_TEXTURE_IMAGE_UNITS:
    case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
    case GL_MAX_TEXTURE_UNITS:
        v[0] = 16;
        return 1;
    case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
        v[0] = 32;
        return 1;
    case GL_MAX_VERTEX_ATTRIBS:
        v[0] = MIME_GPU_MAX_ATTRIBS;
        return 1;
    case GL_MAX_VERTEX_UNIFORM_COMPONENTS:
    case GL_MAX_FRAGMENT_UNIFORM_COMPONENTS:
        v[0] = 1024;
        return 1;
    case GL_MAX_VERTEX_UNIFORM_VECTORS:
    case GL_MAX_FRAGMENT_UNIFORM_VECTORS:
        v[0] = 256;
        return 1;
    case GL_MAX_VARYING_COMPONENTS:
        v[0] = 64;
        return 1;
    case GL_MAX_VARYING_VECTORS:
        v[0] = 16;
        return 1;
    case GL_MAX_DRAW_BUFFERS:
    case GL_MAX_COLOR_ATTACHMENTS:
        v[0] = MIME_GPU_MAX_COLORS;
        return 1;
    case GL_MAX_SAMPLES:
        v[0] = 4;
        return 1;
    case GL_MAX_ELEMENTS_VERTICES:
    case GL_MAX_ELEMENTS_INDICES:
        v[0] = 1 << 20;
        return 1;
    case GL_MAX_TEXTURE_LOD_BIAS:
        v[0] = 16;
        return 1;
    case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT:
        v[0] = 16;
        return 1;
    case GL_ALIASED_LINE_WIDTH_RANGE:
    case GL_SMOOTH_LINE_WIDTH_RANGE:
        v[0] = v[1] = 1;
        return 2;
    case GL_ALIASED_POINT_SIZE_RANGE:
    case GL_POINT_SIZE_RANGE:
        v[0] = 1;
        v[1] = 511;
        return 2;
    case GL_SUBPIXEL_BITS:
        v[0] = 8;
        return 1;
    case GL_SAMPLE_BUFFERS:
        v[0] = 0;
        return 1;
    case GL_SAMPLES:
        v[0] = 0;
        return 1;
    case GL_RED_BITS:
    case GL_GREEN_BITS:
    case GL_BLUE_BITS:
        v[0] = 8;
        return 1;
    case GL_ALPHA_BITS:
        v[0] = ctx->draw_fbo ? 8 : ctx->pfd.cAlphaBits;
        return 1;
    case GL_DEPTH_BITS:
        v[0] = ctx->draw_fbo ? 24 : ctx->pfd.cDepthBits;
        return 1;
    case GL_STENCIL_BITS:
        v[0] = ctx->draw_fbo ? 8 : ctx->pfd.cStencilBits;
        return 1;
    case GL_DOUBLEBUFFER:
        v[0] = ctx->draw_fbo || (ctx->pfd.dwFlags & PFD_DOUBLEBUFFER);
        return 1;
    case GL_STEREO:
        v[0] = 0;
        return 1;
    case GL_VIEWPORT:
        for (int i = 0; i < 4; i++)
            v[i] = ctx->viewport[i];
        return 4;
    case GL_SCISSOR_BOX:
        for (int i = 0; i < 4; i++)
            v[i] = ctx->scissor[i];
        return 4;
    case GL_DEPTH_RANGE:
        v[0] = ctx->depth_range[0];
        v[1] = ctx->depth_range[1];
        return 2;
    case GL_COLOR_CLEAR_VALUE:
        *colors = TRUE;
        for (int i = 0; i < 4; i++)
            v[i] = ctx->clear_color[i];
        return 4;
    case GL_BLEND_COLOR:
        *colors = TRUE;
        for (int i = 0; i < 4; i++)
            v[i] = ctx->blend_color[i];
        return 4;
    case GL_COLOR_WRITEMASK:
        for (int i = 0; i < 4; i++)
            v[i] = ctx->color_mask[i];
        return 4;
    case GL_DEPTH_CLEAR_VALUE:
        v[0] = ctx->clear_depth;
        return 1;
    case GL_STENCIL_CLEAR_VALUE:
        v[0] = ctx->clear_stencil;
        return 1;
    case GL_DEPTH_WRITEMASK:
        v[0] = ctx->depth_mask;
        return 1;
    case GL_DEPTH_FUNC:
        v[0] = ctx->depth_func;
        return 1;
    case GL_STENCIL_FUNC:
        v[0] = ctx->stencil_func[0];
        return 1;
    case GL_STENCIL_REF:
        v[0] = ctx->stencil_ref[0];
        return 1;
    case GL_STENCIL_VALUE_MASK:
        v[0] = ctx->stencil_value_mask[0];
        return 1;
    case GL_STENCIL_WRITEMASK:
        v[0] = ctx->stencil_write_mask[0];
        return 1;
    case GL_STENCIL_FAIL:
        v[0] = ctx->stencil_fail[0];
        return 1;
    case GL_STENCIL_PASS_DEPTH_FAIL:
        v[0] = ctx->stencil_zfail[0];
        return 1;
    case GL_STENCIL_PASS_DEPTH_PASS:
        v[0] = ctx->stencil_zpass[0];
        return 1;
    case GL_STENCIL_BACK_FUNC:
        v[0] = ctx->stencil_func[1];
        return 1;
    case GL_STENCIL_BACK_REF:
        v[0] = ctx->stencil_ref[1];
        return 1;
    case GL_STENCIL_BACK_VALUE_MASK:
        v[0] = ctx->stencil_value_mask[1];
        return 1;
    case GL_STENCIL_BACK_WRITEMASK:
        v[0] = ctx->stencil_write_mask[1];
        return 1;
    case GL_STENCIL_BACK_FAIL:
        v[0] = ctx->stencil_fail[1];
        return 1;
    case GL_STENCIL_BACK_PASS_DEPTH_FAIL:
        v[0] = ctx->stencil_zfail[1];
        return 1;
    case GL_STENCIL_BACK_PASS_DEPTH_PASS:
        v[0] = ctx->stencil_zpass[1];
        return 1;
    case GL_BLEND_SRC_RGB:
        v[0] = ctx->blend.src_rgb;
        return 1;
    case GL_BLEND_DST_RGB:
        v[0] = ctx->blend.dst_rgb;
        return 1;
    case GL_BLEND_SRC_ALPHA:
        v[0] = ctx->blend.src_alpha;
        return 1;
    case GL_BLEND_DST_ALPHA:
        v[0] = ctx->blend.dst_alpha;
        return 1;
    case GL_BLEND_EQUATION_RGB:
        v[0] = ctx->blend.eq_rgb;
        return 1;
    case GL_BLEND_EQUATION_ALPHA:
        v[0] = ctx->blend.eq_alpha;
        return 1;
    case GL_CULL_FACE_MODE:
        v[0] = ctx->cull_mode;
        return 1;
    case GL_FRONT_FACE:
        v[0] = ctx->front_face;
        return 1;
    case GL_POLYGON_MODE:
        v[0] = v[1] = ctx->polygon_mode;
        return 2;
    case GL_POLYGON_OFFSET_FACTOR:
        v[0] = ctx->offset_factor;
        return 1;
    case GL_POLYGON_OFFSET_UNITS:
        v[0] = ctx->offset_units;
        return 1;
    case GL_LINE_WIDTH:
        v[0] = ctx->line_width;
        return 1;
    case GL_POINT_SIZE:
        v[0] = ctx->point_size;
        return 1;
    case GL_POINT_SIZE_MIN:
        v[0] = ctx->point_size_min;
        return 1;
    case GL_POINT_SIZE_MAX:
        v[0] = ctx->point_size_max;
        return 1;
    case GL_POINT_FADE_THRESHOLD_SIZE:
        v[0] = ctx->point_fade_threshold;
        return 1;
    case GL_POINT_DISTANCE_ATTENUATION:
        for (int i = 0; i < 3; i++)
            v[i] = ctx->point_attenuation[i];
        return 3;
    case GL_PACK_ALIGNMENT:
        v[0] = ctx->pack_alignment;
        return 1;
    case GL_UNPACK_ALIGNMENT:
        v[0] = ctx->unpack_alignment;
        return 1;
    case GL_UNPACK_ROW_LENGTH:
        v[0] = ctx->unpack_row_length;
        return 1;
    case GL_PACK_ROW_LENGTH:
        v[0] = ctx->pack_row_length;
        return 1;
    case GL_UNPACK_SKIP_ROWS:
        v[0] = ctx->unpack_skip_rows;
        return 1;
    case GL_UNPACK_SKIP_PIXELS:
        v[0] = ctx->unpack_skip_pixels;
        return 1;
    case GL_UNPACK_IMAGE_HEIGHT:
        v[0] = ctx->unpack_image_height;
        return 1;
    case GL_UNPACK_SKIP_IMAGES:
        v[0] = ctx->unpack_skip_images;
        return 1;
    case GL_PACK_SKIP_ROWS:
        v[0] = ctx->pack_skip_rows;
        return 1;
    case GL_PACK_SKIP_PIXELS:
        v[0] = ctx->pack_skip_pixels;
        return 1;
    case GL_PACK_SWAP_BYTES:
    case GL_PACK_LSB_FIRST:
    case GL_PACK_IMAGE_HEIGHT:
    case GL_PACK_SKIP_IMAGES:
        v[0] = 0;
        return 1;
    case GL_ACTIVE_TEXTURE:
        v[0] = GL_TEXTURE0 + ctx->active_texture;
        return 1;
    case GL_TEXTURE_BINDING_2D:
        v[0] = ctx->units[ctx->active_texture][0];
        return 1;
    case GL_TEXTURE_BINDING_CUBE_MAP:
        v[0] = ctx->units[ctx->active_texture][1];
        return 1;
    case GL_TEXTURE_BINDING_3D:
        v[0] = ctx->units[ctx->active_texture][2];
        return 1;
    case GL_TEXTURE_BINDING_2D_ARRAY:
        v[0] = ctx->units[ctx->active_texture][3];
        return 1;
    case GL_TEXTURE_BINDING_RECTANGLE:
        v[0] = ctx->units[ctx->active_texture][4];
        return 1;
    case GL_ARRAY_BUFFER_BINDING:
        v[0] = ctx->array_buffer;
        return 1;
    case GL_ELEMENT_ARRAY_BUFFER_BINDING:
        v[0] = ctx->vao->element_buffer;
        return 1;
    case GL_PIXEL_PACK_BUFFER_BINDING:
        v[0] = ctx->pixel_pack;
        return 1;
    case GL_PIXEL_UNPACK_BUFFER_BINDING:
        v[0] = ctx->pixel_unpack;
        return 1;
    case GL_UNIFORM_BUFFER_BINDING:
        v[0] = ctx->uniform_buffer;
        return 1;
    case GL_VERTEX_ARRAY_BINDING:
        v[0] = ctx->vao == &ctx->default_vao ? 0 : ctx->vao->name;
        return 1;
    case GL_CURRENT_PROGRAM:
        v[0] = ctx->program;
        return 1;
    case GL_DRAW_FRAMEBUFFER_BINDING:
        v[0] = ctx->draw_fbo;
        return 1;
    case GL_READ_FRAMEBUFFER_BINDING:
        v[0] = ctx->read_fbo;
        return 1;
    case GL_RENDERBUFFER_BINDING:
        v[0] = ctx->renderbuffer;
        return 1;
    case GL_DRAW_BUFFER:
    case GL_DRAW_BUFFER0: {
        GlFramebuffer *fb = ctx->draw_fbo ? gl_framebuffer(ctx, ctx->draw_fbo) : nullptr;
        v[0] = fb ? fb->draw_buffers[0] : ctx->draw_buffer;
        return 1;
    }
    case GL_READ_BUFFER: {
        GlFramebuffer *fb = ctx->read_fbo ? gl_framebuffer(ctx, ctx->read_fbo) : nullptr;
        v[0] = fb ? fb->read_buffer : ctx->read_buffer;
        return 1;
    }
    case GL_IMPLEMENTATION_COLOR_READ_FORMAT:
        v[0] = GL_RGBA;
        return 1;
    case GL_IMPLEMENTATION_COLOR_READ_TYPE:
        v[0] = GL_UNSIGNED_BYTE;
        return 1;
    case GL_NUM_COMPRESSED_TEXTURE_FORMATS:
        v[0] = 0;
        return 1;
    case GL_NUM_SHADER_BINARY_FORMATS:
    case GL_NUM_PROGRAM_BINARY_FORMATS:
        v[0] = 0;
        return 1;
    case GL_MAX_UNIFORM_BLOCK_SIZE:
        v[0] = 16384;
        return 1;
    case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT:
        v[0] = 256;
        return 1;
    case GL_MAX_UNIFORM_BUFFER_BINDINGS:
    case GL_MAX_VERTEX_UNIFORM_BLOCKS:
    case GL_MAX_FRAGMENT_UNIFORM_BLOCKS:
        v[0] = MIME_GPU_MAX_UNIFORM_BLOCKS;
        return 1;
    case GL_MAX_VERTEX_ATTRIB_BINDINGS:
        v[0] = MIME_GPU_MAX_ATTRIBS;
        return 1;
    case GL_MAX_DEBUG_MESSAGE_LENGTH:
        v[0] = 1024;
        return 1;
    case GL_MAX_LABEL_LENGTH:
        v[0] = 256;
        return 1;
    case GL_PRIMITIVE_RESTART_INDEX:
        v[0] = 0xFFFFFFFF;
        return 1;
    case GL_MAX_LIGHTS:
        v[0] = 8;
        return 1;
    case GL_MAX_TEXTURE_COORDS:
        v[0] = 8;
        return 1;
    case GL_PERSPECTIVE_CORRECTION_HINT:
    case GL_POINT_SMOOTH_HINT:
    case GL_LINE_SMOOTH_HINT:
    case GL_POLYGON_SMOOTH_HINT:
    case GL_FOG_HINT:
    case GL_GENERATE_MIPMAP_HINT:
    case GL_TEXTURE_COMPRESSION_HINT:
    case GL_FRAGMENT_SHADER_DERIVATIVE_HINT:
        v[0] = ctx->hints[hint_index(pname)];
        return 1;
    case GL_TIMESTAMP:
        v[0] = (GLdouble)gl_timestamp();
        return 1;
    default: {
        GLboolean b;
        uint32_t dirty;
        BOOL *p = capability(ctx, pname, &dirty);
        if (p) {
            b = *p ? GL_TRUE : GL_FALSE;
            v[0] = b;
            return 1;
        }
        return gl_ff_get(ctx, pname, v, colors);
    }
    }
}

GLAPI void APIENTRY glGetDoublev(GLenum pname, GLdouble *data)
{
    GL_CONTEXT_OR_RETURN();
    GLdouble v[16];
    BOOL colors;
    int n = get_values(ctx, pname, v, &colors);
    if (!n)
        gl_error(ctx, GL_INVALID_ENUM);
    for (int i = 0; i < n; i++)
        data[i] = v[i];
}

GLAPI void APIENTRY glGetFloatv(GLenum pname, GLfloat *data)
{
    GL_CONTEXT_OR_RETURN();
    GLdouble v[16];
    BOOL colors;
    int n = get_values(ctx, pname, v, &colors);
    if (!n)
        gl_error(ctx, GL_INVALID_ENUM);
    for (int i = 0; i < n; i++)
        data[i] = (GLfloat)v[i];
}

GLAPI void APIENTRY glGetIntegerv(GLenum pname, GLint *data)
{
    GL_CONTEXT_OR_RETURN();
    GLdouble v[16];
    BOOL colors;
    int n = get_values(ctx, pname, v, &colors);
    if (!n)
        gl_error(ctx, GL_INVALID_ENUM);
    for (int i = 0; i < n; i++) {
        if (colors)
            data[i] = (GLint)(v[i] * 2147483647.0);
        else if (v[i] >= 4294967295.0)
            data[i] = -1;
        else
            data[i] = (GLint)(int64_t)v[i];
    }
}

GLAPI void APIENTRY glGetInteger64v(GLenum pname, GLint64 *data)
{
    GLint v[16] = {};
    glGetIntegerv(pname, v);
    for (int i = 0; i < 4; i++)
        data[i] = v[i];
}

GLAPI void APIENTRY glGetBooleanv(GLenum pname, GLboolean *data)
{
    GL_CONTEXT_OR_RETURN();
    GLdouble v[16];
    BOOL colors;
    int n = get_values(ctx, pname, v, &colors);
    if (!n)
        gl_error(ctx, GL_INVALID_ENUM);
    for (int i = 0; i < n; i++)
        data[i] = gl_enum_of_bool(v[i] != 0.0);
}

GLAPI void APIENTRY glGetIntegeri_v(GLenum target, GLuint index, GLint *data)
{
    GL_CONTEXT_OR_RETURN();
    if (target == GL_UNIFORM_BUFFER_BINDING || target == GL_UNIFORM_BUFFER_START || target == GL_UNIFORM_BUFFER_SIZE) {
        if (index >= MIME_GPU_MAX_UNIFORM_BLOCKS) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
        GlBufferBinding *binding = &ctx->uniform_bindings[index];
        GlBuffer *buffer = gl_buffer(ctx, binding->name);
        if (data)
            *data = target == GL_UNIFORM_BUFFER_BINDING ? (GLint)binding->name :
                    target == GL_UNIFORM_BUFFER_START ? (GLint)binding->offset :
                    (GLint)(binding->whole && buffer ? buffer->size : binding->size);
        return;
    }
    glGetIntegerv(target, data);
}

GLAPI void APIENTRY glGetBooleani_v(GLenum target, GLuint index, GLboolean *data)
{
    glGetBooleanv(target, data);
}

GLAPI void APIENTRY glGetPointerv(GLenum pname, void **params)
{
    GL_CONTEXT_OR_RETURN();
    *params = nullptr;
    int array = pname == GL_VERTEX_ARRAY_POINTER ? 0 : pname == GL_COLOR_ARRAY_POINTER ? 1
                : pname == GL_NORMAL_ARRAY_POINTER ? 2 : pname == GL_TEXTURE_COORD_ARRAY_POINTER ? 3 : -1;
    if (array == 3)
        array += ctx->ff.client_unit < FF_UNITS ? (int)ctx->ff.client_unit : 0;
    if (array >= 0)
        *params = (void *)ctx->vao->legacy[array].pointer;
}

/* ---------------------------------------------------------------- debug */

GLAPI void APIENTRY glDebugMessageCallback(GLDEBUGPROC callback, const void *user)
{
}

GLAPI void APIENTRY glDebugMessageControl(GLenum source, GLenum type, GLenum severity, GLsizei count,
                                          const GLuint *ids, GLboolean enabled)
{
}

GLAPI void APIENTRY glDebugMessageInsert(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                         const GLchar *buf)
{
}

GLAPI GLuint APIENTRY glGetDebugMessageLog(GLuint count, GLsizei size, GLenum *sources, GLenum *types, GLuint *ids,
                                           GLenum *severities, GLsizei *lengths, GLchar *log)
{
    return 0;
}

GLAPI void APIENTRY glPushDebugGroup(GLenum source, GLuint id, GLsizei length, const GLchar *message)
{
}

GLAPI void APIENTRY glPopDebugGroup(void)
{
}

GLAPI void APIENTRY glObjectLabel(GLenum identifier, GLuint name, GLsizei length, const GLchar *label)
{
}
