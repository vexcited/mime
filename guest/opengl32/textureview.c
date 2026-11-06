/*
 * texture views, GL_ARB_texture_view: a texture that reads levels and layers of the storage of an immutable texture, with
 * another target and a format of the same class. the host gives the new id an image view of the same image.
 */
#include "opengl32.h"

/* the texel size of a Metal format the views and buffer textures use, 0 for a format they do not take. */
uint32_t gl_mtl_texel_bytes(uint32_t mtl)
{
    if (mtl >= 10 && mtl <= 14)
        return 1;
    if ((mtl >= 20 && mtl <= 34) || (mtl >= 40 && mtl <= 42))
        return 2;
    if ((mtl >= 53 && mtl <= 74) || (mtl >= 80 && mtl <= 93))
        return 4;
    if ((mtl >= 103 && mtl <= 105) || (mtl >= 110 && mtl <= 115))
        return 8;
    if (mtl >= 123 && mtl <= 125)
        return 16;
    return 0;
}

typedef struct ViewFormat {
    GLenum internal;
    uint16_t mtl;
    uint8_t klass; /* the bits of a texel, or a class of its own for the formats that only view themselves. */
} ViewFormat;

/* the view classes of the specification's table, with the Metal format each takes on the host. */
static const ViewFormat g_formats[] = {
    {GL_RGBA32F, 125, 128}, {GL_RGBA32UI, 123, 128}, {GL_RGBA32I, 124, 128},
    {GL_RGBA16F, 115, 64},  {GL_RG32F, 105, 64},     {GL_RGBA16UI, 113, 64}, {GL_RG32UI, 103, 64},
    {GL_RGBA16I, 114, 64},  {GL_RG32I, 104, 64},     {GL_RGBA16, 110, 64},   {GL_RGBA16_SNORM, 112, 64},
    {GL_RG16F, 65, 32},     {GL_R11F_G11F_B10F, 92, 32}, {GL_R32F, 55, 32},  {GL_RGB10_A2UI, 91, 32},
    {GL_RGBA8UI, 73, 32},   {GL_RG16UI, 63, 32},     {GL_R32UI, 53, 32},     {GL_RGBA8I, 74, 32},
    {GL_RG16I, 64, 32},     {GL_R32I, 54, 32},       {GL_RGB10_A2, 90, 32},  {GL_RGBA8, 70, 32},
    {GL_RG16, 60, 32},      {GL_RGBA8_SNORM, 72, 32}, {GL_RG16_SNORM, 62, 32}, {GL_SRGB8_ALPHA8, 71, 32},
    {GL_RGB9_E5, 93, 32},
    {GL_RGB8, 70, 24},      {GL_SRGB8, 71, 24},
    {GL_R16F, 25, 16},      {GL_RG8UI, 33, 16},      {GL_R16UI, 23, 16},     {GL_RG8I, 34, 16},
    {GL_R16I, 24, 16},      {GL_RG8, 30, 16},        {GL_R16, 20, 16},       {GL_RG8_SNORM, 32, 16},
    {GL_R16_SNORM, 22, 16},
    {GL_R8UI, 13, 8},       {GL_R8I, 14, 8},         {GL_R8, 10, 8},         {GL_R8_SNORM, 12, 8},
};

static const ViewFormat *view_format(GLenum internal)
{
    for (size_t i = 0; i < sizeof(g_formats) / sizeof(g_formats[0]); i++)
        if (g_formats[i].internal == internal)
            return &g_formats[i];
    return nullptr;
}

/* the targets a view of a texture of the target may have. */
static BOOL target_compatible(GLenum original, GLenum target)
{
    switch (original) {
    case GL_TEXTURE_1D:
    case GL_TEXTURE_1D_ARRAY:
        return target == GL_TEXTURE_1D || target == GL_TEXTURE_1D_ARRAY;
    case GL_TEXTURE_2D:
        return target == GL_TEXTURE_2D || target == GL_TEXTURE_2D_ARRAY;
    case GL_TEXTURE_3D:
        return target == GL_TEXTURE_3D;
    case GL_TEXTURE_RECTANGLE:
        return target == GL_TEXTURE_RECTANGLE;
    case GL_TEXTURE_CUBE_MAP:
    case GL_TEXTURE_2D_ARRAY:
    case GL_TEXTURE_CUBE_MAP_ARRAY:
        return target == GL_TEXTURE_2D || target == GL_TEXTURE_2D_ARRAY || target == GL_TEXTURE_CUBE_MAP ||
               target == GL_TEXTURE_CUBE_MAP_ARRAY;
    case GL_TEXTURE_2D_MULTISAMPLE:
        return target == GL_TEXTURE_2D_MULTISAMPLE;
    default:
        return FALSE;
    }
}

static BOOL arrayed(GLenum target)
{
    return target == GL_TEXTURE_1D_ARRAY || target == GL_TEXTURE_2D_ARRAY || target == GL_TEXTURE_CUBE_MAP_ARRAY;
}

static GLuint layers_of(const GlTexture *t)
{
    if (t->view.is)
        return t->view.layers;
    switch (t->target) {
    case GL_TEXTURE_1D_ARRAY:
        return t->height;
    case GL_TEXTURE_2D_ARRAY:
    case GL_TEXTURE_CUBE_MAP_ARRAY:
    case GL_TEXTURE_CUBE_MAP:
        return t->depth;
    default:
        return 1;
    }
}

BOOL gl_textureview_param(const GlTexture *t, GLenum pname, GLint *value)
{
    switch (pname) {
    case GL_TEXTURE_IMMUTABLE_FORMAT:
        *value = t->immutable_levels != 0;
        return TRUE;
    case GL_TEXTURE_IMMUTABLE_LEVELS:
        *value = (GLint)t->immutable_levels;
        return TRUE;
    case GL_TEXTURE_VIEW_MIN_LEVEL:
        *value = t->view.is ? (GLint)t->view.min_level : 0;
        return TRUE;
    case GL_TEXTURE_VIEW_NUM_LEVELS:
        *value = t->view.is ? (GLint)t->view.levels : 0;
        return TRUE;
    case GL_TEXTURE_VIEW_MIN_LAYER:
        *value = t->view.is ? (GLint)t->view.min_layer : 0;
        return TRUE;
    case GL_TEXTURE_VIEW_NUM_LAYERS:
        *value = t->view.is ? (GLint)t->view.layers : 0;
        return TRUE;
    default:
        return FALSE;
    }
}

GLAPI void APIENTRY glTextureView(GLuint texture, GLenum target, GLuint origtexture, GLenum internal, GLuint minlevel,
                                  GLuint numlevels, GLuint minlayer, GLuint numlayers)
{
    GL_CONTEXT_OR_RETURN();
    if (!gl_host_has(MIME_GPU_FEATURE_TEXTURE_VIEW)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    void *slot = texture ? gl_name_get(&ctx->shared->textures, texture) : nullptr;
    GlTexture *orig = gl_texture(ctx, origtexture);
    if (!slot || !orig) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    /* the name must be one glGenTextures gave and nothing bound yet. */
    if (slot != GL_RESERVED || !orig->immutable_levels || !orig->id) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (!target_compatible(orig->target, target) ||
        (target == GL_TEXTURE_CUBE_MAP_ARRAY && !gl_host_has(MIME_GPU_FEATURE_CUBE_ARRAY))) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    /* the format must be of the class of the original one, and the host must keep both in texels of the same size. */
    const ViewFormat *want = view_format(internal), *have = view_format(orig->internal_format);
    BOOL same = internal == orig->internal_format;
    if (!same && (!want || !have || want->klass != have->klass)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    uint32_t swizzle;
    BOOL compressed;
    uint32_t mtl = want ? want->mtl : gl_mtl_format(internal, GL_RGBA, GL_UNSIGNED_BYTE, &swizzle, &compressed);
    if (!mtl || (!same && gl_mtl_texel_bytes(mtl) != gl_mtl_texel_bytes(orig->mtl_format))) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    GLuint levels = orig->immutable_levels, layers = layers_of(orig);
    if (minlevel >= levels || minlayer >= layers) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (numlevels > levels - minlevel)
        numlevels = levels - minlevel;
    if (numlayers > layers - minlayer)
        numlayers = layers - minlayer;
    if (!numlevels || !numlayers || (!arrayed(target) && target != GL_TEXTURE_CUBE_MAP && numlayers != 1) ||
        (target == GL_TEXTURE_CUBE_MAP && numlayers != 6) || (target == GL_TEXTURE_CUBE_MAP_ARRAY && numlayers % 6)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if ((target == GL_TEXTURE_CUBE_MAP || target == GL_TEXTURE_CUBE_MAP_ARRAY) && orig->width != orig->height) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    GlTexture *t = gl_texture_create(ctx, texture, target);
    if (!t) {
        gl_error(ctx, GL_OUT_OF_MEMORY);
        return;
    }
    gl_mtl_format(internal, GL_RGBA, GL_UNSIGNED_BYTE, &swizzle, &compressed);
    uint32_t w = orig->width >> minlevel, h = orig->height >> minlevel, d = orig->depth >> minlevel;
    t->internal_format = internal;
    t->mtl_format = mtl;
    t->mtl_type = gl_mtl_type_of(target);
    t->swizzle = swizzle;
    t->compressed = orig->compressed;
    t->samples = orig->samples;
    t->width = w ? w : 1;
    t->height = target == GL_TEXTURE_1D_ARRAY ? numlayers : target == GL_TEXTURE_1D ? 1 : h ? h : 1;
    t->depth = target == GL_TEXTURE_3D ? (d ? d : 1)
               : target == GL_TEXTURE_2D_ARRAY || target == GL_TEXTURE_CUBE_MAP_ARRAY || target == GL_TEXTURE_CUBE_MAP
                   ? numlayers
                   : 1;
    t->levels = numlevels;
    t->defined_levels = (uint16_t)((1u << numlevels) - 1);
    t->immutable_levels = numlevels;
    t->view.is = TRUE;
    t->view.min_level = (orig->view.is ? orig->view.min_level : 0) + minlevel;
    t->view.levels = numlevels;
    t->view.min_layer = (orig->view.is ? orig->view.min_layer : 0) + minlayer;
    t->view.layers = numlayers;
    t->id = gl_new_id(ID_TEXTURE);
    gl_device_lock();
    MimeGpuTextureView *c = gl_emit(MIME_GPU_TEXTURE_VIEW, sizeof(MimeGpuTextureView));
    c->id = t->id;
    c->source = orig->id;
    c->type = t->mtl_type;
    c->format = mtl;
    c->min_level = minlevel;
    c->levels = numlevels;
    c->min_layer = target == GL_TEXTURE_3D ? 0 : minlayer;
    c->layers = target == GL_TEXTURE_3D ? 1 : numlayers;
    c->swizzle = gl_texture_sampling_swizzle(t);
    gl_device_unlock();
    ctx->dirty |= DIRTY_TEXTURES;
}
